#ifndef CETTA_ATOM_H
#define CETTA_ATOM_H

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "session.h"
#include "symbol.h"
#include "binding/frame_identity.h"

#ifndef CETTA_PROVENANCE_ASSERT
#define CETTA_PROVENANCE_ASSERT 0
#endif

#ifndef CETTA_BUILD_WITH_GMP
#define CETTA_BUILD_WITH_GMP 0
#endif

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif

typedef struct CettaForeignValue CettaForeignValue;
typedef struct CettaBigInt CettaBigInt;
typedef struct CettaRational CettaRational;
typedef struct CettaPrimeNeedCapability CettaPrimeNeedCapability;
typedef struct CettaPrimeContext CettaPrimeContext;
typedef uint64_t VarId;
typedef uint32_t NameId;
typedef uint64_t CettaExprLen;
typedef uint64_t CettaExprIndex;
typedef struct HashConsTable HashConsTable;
typedef struct ArenaFinalizer ArenaFinalizer;
typedef struct ArenaSymbolCache ArenaSymbolCache;
typedef struct AtomDeepCopySession AtomDeepCopySession;

#define VAR_ID_NONE ((VarId)0)
#define NAME_ID_NONE ((NameId)0)
/* ── Atom kinds ─────────────────────────────────────────────────────────── */

typedef enum {
    ATOM_SYMBOL,
    ATOM_VAR,
    ATOM_GROUNDED,
    ATOM_EXPR
} AtomKind;

typedef enum {
    GV_INT,
    GV_FLOAT,
    GV_BOOL,
    GV_STRING,
    GV_BIGINT,
    GV_SPACE,
    GV_STATE,
    GV_CAPTURE,
    GV_FOREIGN,
    GV_RATIONAL,
    GV_PRIME_NEED_CAPABILITY,
    GV_PRIME_CONTEXT,
    GV_INTERNAL_TAG,
    GV_BINDINGS,
    /* A node of a rational term's graph (term_graph.h). */
    GV_TERM_GRAPH
} GroundedKind;

typedef enum {
    CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND = 1,
    CETTA_INTERNAL_TAG_COUNTED_COLLECTION = 2,
    CETTA_INTERNAL_TAG_PRIME_LEXICAL_SLOT = 3,
    CETTA_INTERNAL_TAG_PRIME_LEVEL_PARAMETER = 4,
    CETTA_INTERNAL_TAG_PETTA_OPEN_CONS = 5,
    /* A list value [x1, ..., xn] is the expression (LIST x1 ... xn), and a list
     * pattern [x1, ..., xk | rest] is (LIST_REST x1 ... xk rest).  No source
     * text spells these tags and no variable binds them, so a list never
     * equals or matches an expression. */
    CETTA_INTERNAL_TAG_LIST = 6,
    CETTA_INTERNAL_TAG_LIST_REST = 7,
    /* A PeTTa operation that has no answer.  In PeTTa `Empty` is data
     * except as a `case` default, so no-result cannot be that symbol. */
    CETTA_INTERNAL_TAG_PETTA_NO_RESULT = 8,
    /* A rational term's node with the values of the free variables it
     * reaches: (tag node value...), term_graph.h. */
    CETTA_INTERNAL_TAG_RATIONAL = 9,
    /* A retained PeTTa partial value. Authored `(partial ...)` is a list,
     * while the reference's partial/2 is a compound. */
    CETTA_INTERNAL_TAG_PETTA_PARTIAL = 10,
    /* Private nominal domain of neutral Lam code, and a nullary callable.
     * Source expressions cannot manufacture these value constructors. */
    CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY = 11,
    CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE = 12,
    CETTA_INTERNAL_TAG_PRIME_HELD = 13,
} CettaInternalTag;

static inline bool cetta_internal_tag_is_list(int64_t tag) {
    return tag == (int64_t)CETTA_INTERNAL_TAG_LIST ||
           tag == (int64_t)CETTA_INTERNAL_TAG_LIST_REST;
}

static inline bool cetta_internal_tag_is_callable(int64_t tag) {
    return tag == (int64_t)CETTA_INTERNAL_TAG_PETTA_PARTIAL ||
           tag == (int64_t)CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY ||
           tag == (int64_t)CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE;
}

static inline bool cetta_internal_tag_is_term_stable(int64_t tag) {
    return cetta_internal_tag_is_list(tag) ||
           cetta_internal_tag_is_callable(tag);
}

#define ATOM_FLAG_HAS_VARS 0x01u
#define ATOM_FLAG_HASH_VALID 0x02u
#define ATOM_FLAG_HAS_REGISTRY_REFS 0x04u
#define ATOM_FLAG_HASH_STABLE 0x08u
#define ATOM_FLAG_HASHCONS_ELIGIBLE 0x10u
#define ATOM_FLAG_HAS_PRIVATE_VARIANT_VAR 0x20u
/*
 * Every Atom child reachable through this node is either globally owned or
 * carries the same nonzero arena_id as this node.  This is a compositional
 * proof for generational-copy fast paths; shallow arena ownership alone is
 * not a transitive-lifetime guarantee.
 */
#define ATOM_FLAG_ARENA_CLOSED 0x40u
/* Compositional resource facts, generated at leaves and inherited by every
 * expression.  These make admission and copy-stability tests O(1), independent
 * of semantic term depth. */
#define ATOM_FLAG_HAS_IDENTITY_GROUNDED 0x80u
#define ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE 0x100u
/*
 * Conservative variable-support summary.  Each variable contributes two of
 * the otherwise-unused high flag bits; expressions inherit their children's
 * bits by OR.  A missing bit proves absence, while a collision merely asks the
 * caller to use its exact traversal.  Keeping this inside `flags` preserves
 * the Atom layout and makes the summary free to copy with the term.
 */
#define ATOM_FLAG_VAR_BLOOM_SHIFT 9u
#define ATOM_FLAG_VAR_BLOOM_WIDTH 23u
#define ATOM_FLAG_VAR_BLOOM_MASK UINT32_C(0xFFFFFE00)
/*
 * Constructor-built atoms establish term-retention stability under the same
 * leaf and child-fold judgment as structural-hash stability.  Keep the
 * semantic name explicit for callers; split the bits before either judgment
 * is widened.  The summary assumes expression children remain immutable.
 */
#define ATOM_FLAG_TERM_STABLE ATOM_FLAG_HASH_STABLE

/* Exact, constructor-derived structural facts occupy the natural padding
 * before Atom's payload on 64-bit targets.  The validity bit makes hand-built
 * or unfinished atoms conservative unknowns rather than false negatives. */
#define ATOM_STRUCTURAL_FACTS_VALID UINT32_C(0x80000000)
#define ATOM_STRUCTURAL_HAS_INTERNAL_TAG UINT32_C(0x00000001)
#define ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID UINT32_C(0x00000002)
/* A NaN float leaf, a state cell, whose value may become one, or a rational
 * term's node, whose leaves may hold one.  A NaN equals nothing, not even
 * itself, so an atom is value-equal to itself only when it holds none. */
#define ATOM_STRUCTURAL_HAS_NAN UINT32_C(0x00000004)
/* The atom is or contains a list pattern with a rest, (LIST_REST x... rest). */
#define ATOM_STRUCTURAL_HAS_OPEN_LIST UINT32_C(0x00000040)
/* The atom is or contains a list or a list pattern. */
#define ATOM_STRUCTURAL_HAS_LIST UINT32_C(0x00000080)
/* The atom's arena has an older generation, and every Atom child reachable
 * through this node is globally owned, in this node's arena, or in that older
 * generation, closed there in turn.  Set only where ATOM_FLAG_ARENA_CLOSED is
 * not: that bit keeps its one-arena meaning for every other reader.  Unlike
 * the facts above, which a node has when any child has them, it holds only
 * when it holds of every child. */
#define ATOM_STRUCTURAL_GENERATION_CLOSED UINT32_C(0x00000008)
/* A PeTTa list carrier's tag, the head of the cell `(tag h t)`: a subset of
 * the internal tags, so a reader of list carriers skips every other
 * internal tag's atom. */
#define ATOM_STRUCTURAL_HAS_LIST_CARRIER UINT32_C(0x00000010)
/* The expression's children are the top of a buffer with free slots below
 * them, so a prepend may claim the slot just below its first child when that
 * slot is free (PrefixBuffer.claimable_iff_free_below).  A fact of the node
 * alone: no expression holding it as a child inherits it. */
#define ATOM_STRUCTURAL_FRONT_SLACK UINT32_C(0x00000020)
/* A node of a rational term (term_graph.h), so the term is not a finite
 * tree.  Such an atom is equal to, and hashes as, every other atom with the
 * same unfolding, whatever part of it is open. */
#define ATOM_STRUCTURAL_HAS_RATIONAL UINT32_C(0x00000100)
#define ATOM_STRUCTURAL_HAS_PETTA_NONLIST UINT32_C(0x00000200)
/* Each fact has its own bit: for one-bit flags the sum equals the union
 * exactly when no two share one. */
_Static_assert(ATOM_STRUCTURAL_FACTS_VALID + ATOM_STRUCTURAL_HAS_INTERNAL_TAG +
                       ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID + ATOM_STRUCTURAL_HAS_NAN +
                       ATOM_STRUCTURAL_HAS_OPEN_LIST + ATOM_STRUCTURAL_HAS_LIST +
                       ATOM_STRUCTURAL_GENERATION_CLOSED +
                       ATOM_STRUCTURAL_HAS_LIST_CARRIER + ATOM_STRUCTURAL_FRONT_SLACK +
                       ATOM_STRUCTURAL_HAS_RATIONAL +
                       ATOM_STRUCTURAL_HAS_PETTA_NONLIST ==
                   (ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_HAS_INTERNAL_TAG |
                    ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID | ATOM_STRUCTURAL_HAS_NAN |
                    ATOM_STRUCTURAL_HAS_OPEN_LIST | ATOM_STRUCTURAL_HAS_LIST |
                    ATOM_STRUCTURAL_GENERATION_CLOSED |
                    ATOM_STRUCTURAL_HAS_LIST_CARRIER | ATOM_STRUCTURAL_FRONT_SLACK |
                    ATOM_STRUCTURAL_HAS_RATIONAL |
                    ATOM_STRUCTURAL_HAS_PETTA_NONLIST),
               "structural fact bits overlap");

/*
 * VariantShape reserves this VarId prefix for its runtime-private slots.
 * Keeping the namespace test beside VarId lets immutable atoms summarize the
 * property compositionally instead of rescanning an arbitrarily deep value at
 * every Bindings boundary.
 */
#define CETTA_VARIANT_PRIVATE_SLOT_MASK UINT64_C(0xFFFFFFFF00000000)
#define CETTA_VARIANT_PRIVATE_SLOT_TAG UINT64_C(0xFFFFA11A00000000)

static inline bool atom_var_id_is_private_variant(VarId id) {
    return (id & CETTA_VARIANT_PRIVATE_SLOT_MASK) ==
           CETTA_VARIANT_PRIVATE_SLOT_TAG;
}

/* ── Atom ───────────────────────────────────────────────────────────────── */

typedef struct Atom Atom;
struct Atom {
    AtomKind kind;
    uint32_t flags;
    /*
     * ATOM_VAR: the variable identity.
     * ATOM_EXPR: a singleton-variable support summary.  A nonzero value
     * means every variable occurrence in the expression has this id; zero
     * means either no variables (distinguished by ATOM_FLAG_HAS_VARS) or,
     * with variables, support containing more than one id or not settled:
     * a suffix view of a list with several ids may leave it unsettled
     * (atom_expr_suffix).  Readers take zero with variables as a subterm to
     * walk.  The summary is derived from immutable children and does not
     * change equality or hashing.
     */
    VarId var_id;
    SymbolId sym_id;         /* ATOM_SYMBOL, or variable spelling */
    /*
     * Allocation identity, not part of atom equality or hashing.  Arena
     * identities make ownership tests for known Atom pointers O(1), avoiding
     * a scan of every arena block during graph evacuation.  Zero denotes an
     * atom not owned by an Arena (for example a hash-consed global).
     */
    uint32_t arena_id;
    Atom *name_key;          /* ATOM_VAR structural spelling, otherwise NULL */
    uint32_t hash_cache;     /* lazily memoized structural hash */
    uint32_t structural_facts;
    union {
        struct {            /* ATOM_GROUNDED */
            GroundedKind gkind;
            /* GV_STRING: the byte length of `sval`.  A string is its bytes,
             * embedded NUL included; the byte after them is a NUL kept only
             * for C interoperation.  The field sits in the padding after the
             * kind, so it costs no space. */
            uint32_t slen;
            union {
                int64_t ival;
                double fval;
                const char *sval;
                CettaBigInt *bigint;
                CettaRational *rational;
                CettaPrimeNeedCapability *prime_need_capability;
                CettaPrimeContext *prime_context;
                bool bval;
                void *ptr;
            };
        } ground;
        struct {            /* ATOM_EXPR */
            Atom **elems;
            CettaExprLen len;
        } expr;
    };
};

/* The string length lives in the grounded kind's padding: a grounded payload
 * stays sixteen bytes. */
_Static_assert(sizeof(((Atom *)0)->ground) == 16,
               "the string length must fit the grounded padding");

static inline VarId atom_single_variable_id(const Atom *atom) {
    if (!atom || (atom->flags & ATOM_FLAG_HAS_VARS) == 0u)
        return VAR_ID_NONE;
    if (atom->kind == ATOM_VAR || atom->kind == ATOM_EXPR)
        return atom->var_id;
    return VAR_ID_NONE;
}

static inline bool atom_structural_may_have_internal_tag(
        const Atom *atom) {
    return !atom ||
           (atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) == 0u ||
           (atom->structural_facts &
            ATOM_STRUCTURAL_HAS_INTERNAL_TAG) != 0u;
}

/* Conservative: true unless the atom is known to contain no list pattern
 * with a rest. */
static inline bool atom_structural_may_have_open_list(const Atom *atom) {
    return !atom ||
           (atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) == 0u ||
           (atom->structural_facts & ATOM_STRUCTURAL_HAS_OPEN_LIST) != 0u;
}

/* Conservative: true unless the atom is known to contain no list. */
static inline bool atom_structural_may_have_list(const Atom *atom) {
    return !atom ||
           (atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) == 0u ||
           (atom->structural_facts & ATOM_STRUCTURAL_HAS_LIST) != 0u;
}

static inline bool atom_structural_may_have_list_carrier(
        const Atom *atom) {
    return !atom ||
           (atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) == 0u ||
           (atom->structural_facts &
            ATOM_STRUCTURAL_HAS_LIST_CARRIER) != 0u;
}

static inline bool atom_structural_may_have_nan(const Atom *atom) {
    return !atom ||
           (atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) == 0u ||
           (atom->structural_facts & ATOM_STRUCTURAL_HAS_NAN) != 0u;
}

/* The atom holds a rational term's node.  Exact where the facts are known;
 * an atom without facts is read as it is built, and a node below it answers
 * for itself. */
static inline bool atom_structural_has_rational(const Atom *atom) {
    return atom &&
           (atom->structural_facts & (ATOM_STRUCTURAL_FACTS_VALID |
                                      ATOM_STRUCTURAL_HAS_RATIONAL)) ==
               (ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_HAS_RATIONAL);
}

static inline bool atom_is_internal_tag(
        const Atom *atom, CettaInternalTag tag) {
    return atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_INTERNAL_TAG &&
           atom->ground.ival == (int64_t)tag;
}

/* A list value, (LIST x1 ... xn). */
static inline bool atom_is_list(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len >= 1u &&
           atom_is_internal_tag(atom->expr.elems[0], CETTA_INTERNAL_TAG_LIST);
}

/* A list pattern with a rest, (LIST_REST x1 ... xk rest), k >= 1. */
static inline bool atom_is_list_rest(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len >= 3u &&
           atom_is_internal_tag(atom->expr.elems[0], CETTA_INTERNAL_TAG_LIST_REST);
}

/* A Prolog compound PeTTa holds (Predicate's value, or one Prolog hands
 * back): a term, not a sequence.  The reference's non_list/1 holds of it. */
static inline bool atom_is_petta_prolog_compound(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 2u &&
           atom_is_internal_tag(atom->expr.elems[0],
                                CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND);
}

static inline bool atom_is_petta_partial(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 3u &&
           atom_is_internal_tag(atom->expr.elems[0],
                                CETTA_INTERNAL_TAG_PETTA_PARTIAL);
}

/* Public value role is independent of the expression-shaped executable
 * representation. Private tags establish that role; public head spellings do
 * not. A constructor-derived negative fact keeps ordinary matching O(1). */
typedef enum {
    PETTA_VALUE_ORDINARY = 0,
    PETTA_VALUE_REGISTERED_CALLABLE,
    PETTA_VALUE_COMPOUND,
} PeTTaValueRepresentation;

static inline PeTTaValueRepresentation atom_petta_value_representation(
        const Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR ||
        ((atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u &&
         (atom->structural_facts & ATOM_STRUCTURAL_HAS_PETTA_NONLIST) == 0u))
        return PETTA_VALUE_ORDINARY;
    if (atom_is_petta_prolog_compound(atom) || atom_is_petta_partial(atom))
        return PETTA_VALUE_COMPOUND;
    if (atom->expr.len == 3u) {
        if (atom_is_internal_tag(atom->expr.elems[0],
                CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE))
            return PETTA_VALUE_REGISTERED_CALLABLE;
        const Atom *domain = atom->expr.elems[1];
        if (domain && domain->kind == ATOM_EXPR && domain->expr.len == 2u &&
            atom_is_internal_tag(domain->expr.elems[0],
                CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY))
            return PETTA_VALUE_REGISTERED_CALLABLE;
    }
    return PETTA_VALUE_ORDINARY;
}

/* Only privately constructed callable values carry an identity. Authored
 * Lam/partial expressions do not acquire one from their public spelling. */
static inline bool atom_petta_callable_identity(
        const Atom *atom, int64_t *identity) {
    if (!identity || atom_petta_value_representation(atom) !=
                         PETTA_VALUE_REGISTERED_CALLABLE)
        return false;
    const Atom *token = atom_is_internal_tag(atom->expr.elems[0],
        CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE)
        ? atom->expr.elems[1] : atom->expr.elems[1]->expr.elems[1];
    if (!token || token->kind != ATOM_GROUNDED ||
        token->ground.gkind != GV_INT || token->ground.ival <= 0)
        return false;
    *identity = token->ground.ival;
    return true;
}

/* Call after dereferencing variables. A variable may bind a whole value;
 * expression decomposition must preserve its public constructor class. */
static inline bool atom_petta_decomposition_compatible(
        const Atom *left, const Atom *right) {
    return atom_petta_value_representation(left) ==
           atom_petta_value_representation(right);
}

/* The elements a sequence primitive (car-atom, size-atom, ...) reads: an
 * expression's, or a list's after its tag.  False for any other atom,
 * including a list pattern, whose length is not known, and a Prolog
 * compound, which is a term, not a sequence. */
static inline bool atom_sequence_view(const Atom *atom, Atom *const **elems,
                                      CettaExprLen *len) {
    if (!atom || atom->kind != ATOM_EXPR || atom_is_list_rest(atom) ||
        atom_petta_value_representation(atom) != PETTA_VALUE_ORDINARY)
        return false;
    bool list = atom_is_list(atom);
    *elems = atom->expr.elems + (list ? 1u : 0u);
    *len = atom->expr.len - (list ? 1u : 0u);
    return true;
}

/* A list or a list pattern: data the interpreter never calls. */
static inline bool atom_is_list_form(const Atom *atom) {
    return atom_is_list(atom) || atom_is_list_rest(atom);
}

/* Either list tag, which no variable may be bound to. */
static inline bool atom_is_list_tag(const Atom *atom) {
    return atom_is_internal_tag(atom, CETTA_INTERNAL_TAG_LIST) ||
           atom_is_internal_tag(atom, CETTA_INTERNAL_TAG_LIST_REST);
}

/* The elements of a list value (after its tag), and their number. */
static inline CettaExprLen atom_list_len(const Atom *list) {
    return list->expr.len - 1u;
}
static inline Atom *const *atom_list_elems(const Atom *list) {
    return list->expr.elems + 1;
}

static inline bool atom_is_petta_no_result(const Atom *atom) {
    return atom_is_internal_tag(atom, CETTA_INTERNAL_TAG_PETTA_NO_RESULT);
}

static inline uint32_t atom_var_bloom_for_id(VarId id) {
    if (id == VAR_ID_NONE)
        return 0u;
    uint64_t mixed = (uint64_t)id;
    mixed ^= mixed >> 33u;
    mixed *= UINT64_C(0xff51afd7ed558ccd);
    mixed ^= mixed >> 29u;
    uint32_t first =
        (uint32_t)(mixed % ATOM_FLAG_VAR_BLOOM_WIDTH);
    uint32_t second =
        (uint32_t)((mixed >> 32u) % ATOM_FLAG_VAR_BLOOM_WIDTH);
    return (UINT32_C(1) << (ATOM_FLAG_VAR_BLOOM_SHIFT + first)) |
           (UINT32_C(1) << (ATOM_FLAG_VAR_BLOOM_SHIFT + second));
}

static inline uint32_t atom_variable_bloom(const Atom *atom) {
    return atom ? atom->flags & ATOM_FLAG_VAR_BLOOM_MASK : 0u;
}

static inline bool atom_variable_bloom_may_contain(
        const Atom *atom, VarId id) {
    uint32_t required = atom_var_bloom_for_id(id);
    return required != 0u &&
           (atom_variable_bloom(atom) & required) == required;
}

/* ── Arena allocator ────────────────────────────────────────────────────── */

#define ARENA_BLOCK_SIZE (64 * 1024)

typedef enum {
    CETTA_ARENA_RUNTIME_KIND_OTHER = 0,
    CETTA_ARENA_RUNTIME_KIND_PERSISTENT = 1,
    CETTA_ARENA_RUNTIME_KIND_EVAL = 2,
    CETTA_ARENA_RUNTIME_KIND_SCRATCH = 3,
    CETTA_ARENA_RUNTIME_KIND_SURVIVOR = 4,
} CettaArenaRuntimeKind;

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t capacity;
    size_t used;
    char data[ARENA_BLOCK_SIZE];
} ArenaBlock;

typedef struct ArenaRetainedOwner ArenaRetainedOwner;
typedef struct ArenaFrameIdentitySet ArenaFrameIdentitySet;

typedef struct {
    ArenaBlock *head;
    ArenaBlock *spare;
    HashConsTable *hashcons;
    ArenaSymbolCache *symbol_cache;
    size_t symbol_cache_bytes;
    size_t live_bytes;
    /* Heap storage owned by arena finalizers (for example GMP limbs).  This
     * is kept separate from block occupancy so mark/reset can account for
     * allocation pressure without corrupting the block allocator's math. */
    size_t external_bytes;
    size_t reserved_bytes;
    size_t spare_bytes;
    uint32_t block_count;
    uint32_t spare_block_count;
    CettaArenaRuntimeKind runtime_kind;
    uint32_t identity;
    /* The older generation, whose storage this arena's atoms may share: it
     * is released only together with this arena.  Zero when there is none.
     * Linked once after initialization; an older generation has none of its
     * own. */
    uint32_t older_identity;
    /* The capacity of a new block; zero for ARENA_BLOCK_SIZE
     * (arena_set_block_capacity). */
    uint32_t block_capacity;
    /*
     * Monotone allocation epoch.  Arena identity survives mark/reset, while
     * reset_epoch changes whenever reset or free invalidates owned pointers.
     * Derived pointer caches must key on both.
     */
    uint64_t reset_epoch;
    ArenaFinalizer *finalizers;
    ArenaRetainedOwner *retained_owners;
    /* Frame-identity ownership for this arena's live allocation region.  The
     * set holds one reference for each distinct full generational identity
     * reachable from syntax allocated here, so allocating many variable
     * occurrences of one identity costs one retain instead of one per atom.
     * Membership is truncated on partial reset and cleared on free. */
    ArenaFrameIdentitySet *frame_identities;
    /* Canonical scalar atoms: this arena returns one shared atom per symbol
     * and per small integer until its next reset.  Opt-in, for arenas whose
     * owner allocates such scalars at a high rate. */
    bool scalar_cache;
} Arena;

typedef struct {
    ArenaBlock *head;
    size_t used;
    size_t live_bytes;
    size_t external_bytes;
    size_t symbol_cache_bytes;
    size_t reserved_bytes;
    uint32_t block_count;
    ArenaFinalizer *finalizers;
    /* Distinct frame identities this arena owned at the mark.  A reset keeps
     * exactly this many holds, so ownership acquired after the mark is
     * released and ownership acquired before it stays valid. */
    uint32_t frame_identity_len;
} ArenaMark;

/* An immutable saved substitution. The binding layer owns its representation;
 * atom copying only retains the owner, and printing asks for a projection.
 * Ordinary syntax substitution cannot traverse or rewrite its domain. */
typedef struct CettaBindingsValue {
    void (*retain)(void *);
    void (*release)(void *);
    Atom *(*observe)(Arena *, const struct CettaBindingsValue *);
    bool (*equal)(const struct CettaBindingsValue *,
                  const struct CettaBindingsValue *);
    const VarId *support;
    size_t support_count;
} CettaBindingsValue;

Atom *atom_bindings_value(Arena *arena, CettaBindingsValue *value);

/* Report an allocation of `size` bytes that failed and abort. */
_Noreturn void cetta_oom(size_t size);
void *cetta_malloc(size_t size);
void *cetta_realloc(void *ptr, size_t size);
void  arena_init(Arena *a);
/* Initialize an arena whose allocation identity must not inherit the
 * calling thread's hash-cons table.  Use this for process-lifetime and
 * cross-thread owners: an ordinary arena_init deliberately inherits the
 * current evaluator's table for branch-local sharing. */
void  arena_init_detached(Arena *a);
/* An arena that holds little takes blocks of `capacity` bytes rather than
 * ARENA_BLOCK_SIZE; an allocation larger than that still gets a block of its
 * own size. */
void  arena_set_block_capacity(Arena *a, size_t capacity);
void  arena_free(Arena *a);
void  arena_reserve(Arena *a, size_t size);
void  arena_set_hashcons(Arena *a, HashConsTable *hc);
void  arena_set_runtime_kind(Arena *a, CettaArenaRuntimeKind kind);
/* Name `older` as the older generation of `young`, whose atoms may then share
 * its storage.  The owner releases `older` only together with `young`.  An
 * arena that has an older generation cannot serve as one: the link is then
 * left unset. */
void  arena_set_older_generation(Arena *young, const Arena *older);
/* Share one atom per symbol and per small integer within each reset epoch of
 * this arena.  Atoms are immutable, so sharing is invisible to observers;
 * a reset or free forgets every shared atom with the storage it lived in. */
void  arena_set_scalar_cache(Arena *a, bool enabled);
/* Keep a contextual identity alive until this arena's corresponding reset.
 * Ambient/external identifiers have no recyclable owner to retain. */
bool arena_retain_frame_identity(Arena *a, CettaFrameIdentity identity);
#ifdef CETTA_TEST_HOOKS
/* Ownership observations for the arena frame-identity lifetime tests. */
uint32_t arena_frame_identity_count_test(const Arena *a);
uint32_t arena_frame_identity_capacity_test(const Arena *a);
bool arena_frame_identity_owned_test(const Arena *a,
                                     CettaFrameIdentity identity);
#endif
/* Retain an immutable external owner once per arena lifetime segment. A reset
 * releases owners first retained after its mark; earlier owners remain live.
 * Ordinary atom copies must still transport their payload into the destination. */
bool arena_retain_owner(Arena *a, void *owner,
                        void (*retain)(void *), void (*release)(void *));

/* Allocation policy consults these aggregates at every machine transition.
 * Keep the overflow behavior visible to the compiler while atom.c retains
 * external definitions for binary callers. */
inline size_t arena_accounted_live_bytes(const Arena *a) {
    if (!a)
        return 0u;
    size_t live = a->live_bytes;
    if (live > SIZE_MAX - a->external_bytes)
        return SIZE_MAX;
    live += a->external_bytes;
    if (live > SIZE_MAX - a->symbol_cache_bytes)
        return SIZE_MAX;
    return live + a->symbol_cache_bytes;
}

inline size_t arena_mark_accounted_live_bytes(ArenaMark mark) {
    size_t live = mark.live_bytes;
    if (live > SIZE_MAX - mark.external_bytes)
        return SIZE_MAX;
    live += mark.external_bytes;
    if (live > SIZE_MAX - mark.symbol_cache_bytes)
        return SIZE_MAX;
    return live + mark.symbol_cache_bytes;
}

void  arena_account_external_bytes(Arena *a, size_t size);
/* Native handle identifiers retain an external owner without changing their
 * integer equality, hash, or printed representation. Only these identifiers
 * have the extended allocation; ordinary Atom layout is unchanged. */
Atom *atom_native_handle_identifier(Arena *a, int64_t id, void *owner,
                                    void (*retain)(void *),
                                    void (*release)(void *));
Atom *atom_int_copy(Arena *a, const Atom *source);
ArenaMark arena_mark(const Arena *a);
void  arena_reset(Arena *a, ArenaMark mark);
/* Free the arena's unused blocks beyond the first `keep_bytes` of spare
 * capacity; the blocks in use are untouched. */
void  arena_release_spare(Arena *a, size_t keep_bytes);
/* Whether `a` is exactly at `mark`: nothing allocated, finalized or acquired
 * since, so a reset to it would release nothing and every atom stays
 * valid. */
bool  arena_at_mark(const Arena *a, ArenaMark mark);
void *arena_alloc(Arena *a, size_t size);
/* Upper bound on live arena bytes allocated by atom_expr with hash-consing
 * disabled.  Block reservation overhead is not part of this logical charge. */
bool atom_expr_allocation_bound(CettaExprLen length, size_t *bytes_out);
char *arena_strdup(Arena *a, const char *s);
bool  arena_owns_ptr(const Arena *a, const void *ptr);
bool  arena_owns_atom(const Arena *a, const Atom *atom);
/* Exact compositional ownership predicate: every arena-owned pointer in the
 * atom graph belongs to `arena`; globally owned immutable nodes are also
 * permitted.  Identity-bearing external resources deliberately fail. */
bool  atom_graph_is_closed_for_arena(const Arena *arena,
                                     const Atom *atom);
/* Whether a copy into `arena` may share `atom` as it is: the atom is closed
 * in `arena`, or in `arena`'s older generation, across the generations it
 * reaches.  Globally owned atoms are not shared this way. */
bool  atom_settled_for_arena(const Arena *arena, const Atom *atom);
/* Exact bounds of every arena identity reachable through ordinary atom-owned
 * storage.  Identity-bearing external resources are intentionally
 * unsupported. */
bool  atom_graph_arena_id_bounds(const Atom *atom,
                                 uint32_t *min_id,
                                 uint32_t *max_id);
char *cetta_bigint_canonicalize_owned(const char *text);
bool  cetta_bigint_text_fits_i64(const char *text, int64_t *out);
int   cetta_bigint_compare_cstr(const char *lhs, const char *rhs);
char *cetta_rational_canonicalize_owned(const char *text);
int   cetta_rational_compare_cstr(const char *lhs, const char *rhs);
int   cetta_format_float(char *buf, size_t size, double value);

/* ── Hash-Consing (structural sharing for immutable atoms) ─────────────── */

#define HASHCONS_TABLE_SIZE 65536

struct HashConsTable {
    CettaFrameIdentityScope frame_identities;
    Atom **table;
    uint32_t size, used;
    Atom **symbol_cache;
    uint32_t symbol_cache_size;
    Atom **small_int_cache;
    Atom *bool_cache[2];
    uint64_t lookup_count;
    uint64_t lookup_probes;
    uint64_t maximum_lookup_probe;
};

void hashcons_init(HashConsTable *hc);
/* A private ownership domain starts small and uses the same growing table.
 * Canonical atoms retain their addresses when its index grows. */
void hashcons_init_compact(HashConsTable *hc);
void hashcons_free(HashConsTable *hc);
/*
 * Return a shared atom if an identical one exists, otherwise insert it.
 * Expressions and variable name keys are published only when their retained
 * pointers already have global, closed ownership; inadmissible atoms are
 * returned unchanged.  With multiple hash-cons tables, retained children must
 * belong to the same ownership domain or to one that outlives this table.
 */
Atom *hashcons_get(HashConsTable *hc, Atom *atom);

/* Stable value-grounded leaves admitted by the term-retention summary. */
bool atom_grounded_kind_is_term_stable(GroundedKind kind);

/* Global hash-cons table */
extern _Thread_local HashConsTable *g_hashcons;

/* Fast equality: if both atoms are hash-consed, pointer comparison suffices */
bool atom_eq_fast(Atom *a, Atom *b);

/* Compute structural hash of an atom */
uint32_t atom_hash(Atom *a);

/* ── Variable identity intern/freshening ──────────────────────────────── */

typedef _Atomic(VarId) VarInternEntry;

typedef struct {
    _Atomic(VarInternEntry *) *chunks;
    pthread_mutex_t write_mutex;
} VarInternTable;

void    var_intern_init(VarInternTable *t);
void    var_intern_free(VarInternTable *t);
VarId   var_intern(VarInternTable *t, SymbolId spelling);
bool    fresh_var_id_try(VarId *id_out);
VarId   fresh_var_id(void);

/* VarId packs the stable source identity in the low word and an activation
 * epoch in the high word.  These accessors are inline because matching and
 * substitution consult the coordinate at every variable edge; atom.c emits
 * external definitions as well for binary callers. */
inline uint32_t var_base_id(VarId id) {
    return (uint32_t)(id & UINT32_MAX);
}

inline uint32_t var_epoch_suffix(VarId id) {
    return (uint32_t)(id >> 32);
}

inline VarId var_epoch_id(VarId id, uint32_t epoch) {
    uint32_t base = var_base_id(id);
    if (base == 0u)
        base = (uint32_t)fresh_var_id();
    return ((VarId)epoch << 32) | (VarId)base;
}

#ifdef CETTA_TEST_HOOKS
/* Single-threaded boundary hook; production builds cannot reset identity. */
void fresh_var_id_test_reset(uint64_t next_base);
#endif

extern VarInternTable *g_var_intern;

/* ── Constructors ───────────────────────────────────────────────────────── */

Atom *atom_symbol(Arena *a, const char *name);
Atom *atom_symbol_id(Arena *a, SymbolId sym_id);
Atom *atom_var(Arena *a, const char *name);
Atom *atom_var_with_id(Arena *a, const char *name, VarId id);
Atom *atom_var_with_spelling(Arena *a, SymbolId spelling, VarId id);
/* A variable spelled by a string literal, whose symbol is cached by the
 * literal's address: `name` must be storage whose contents never change. */
Atom *atom_var_with_literal(Arena *a, const char *name, VarId id);
Atom *atom_var_with_name_key(Arena *a, Atom *name_key, VarId id);
Atom *atom_var_with_presentation(Arena *a, SymbolId spelling,
                                 Atom *name_key, VarId id);
Atom *atom_var_like(Arena *a, Atom *source, VarId id);
Atom *atom_int(Arena *a, int64_t val);
Atom *atom_bigint(Arena *a, const char *val);
const char *atom_bigint_cstr(const Atom *atom);
Atom *atom_bigint_copy(Arena *a, const Atom *atom);
#if CETTA_BUILD_WITH_GMP
bool  atom_bigint_get_mpz(const Atom *atom, mpz_t out);
mpz_srcptr atom_bigint_mpz_view(const Atom *atom);
Atom *atom_bigint_from_mpz(Arena *a, const mpz_t value);
Atom *atom_bigint_take_mpz(Arena *a, mpz_t value);
#endif
Atom *atom_rational(Arena *a, const char *val);
const char *atom_rational_cstr(const Atom *atom);
#if CETTA_BUILD_WITH_GMP
bool  atom_rational_get_mpq(const Atom *atom, mpq_t out);
Atom *atom_rational_from_mpq(Arena *a, const mpq_t value);
#endif
Atom *atom_float(Arena *a, double val);
Atom *atom_bool(Arena *a, bool val);
/* A string atom of the given bytes, which may hold NUL.  `atom_string` takes
 * a NUL-terminated C string. */
#define CETTA_STRING_BYTES_MAX 0x7FFFFFFFu
Atom *atom_string_n(Arena *a, const char *bytes, size_t len);
Atom *atom_string(Arena *a, const char *val);
static inline size_t atom_string_len(const Atom *atom) {
    return atom->ground.slen;
}
/* Two strings are equal when they have the same bytes; strings order
 * bytewise, a proper prefix first, which is strcmp's order when neither holds
 * NUL. */
static inline bool atom_string_equal(const Atom *left, const Atom *right) {
    return left->ground.slen == right->ground.slen &&
           memcmp(left->ground.sval, right->ground.sval,
                  left->ground.slen) == 0;
}
static inline bool atom_string_equals_bytes(const Atom *atom,
                                            const char *bytes, size_t len) {
    return atom->ground.slen == len &&
           memcmp(atom->ground.sval, bytes, len) == 0;
}
static inline bool atom_string_equals_cstr(const Atom *atom,
                                           const char *text) {
    return atom_string_equals_bytes(atom, text, strlen(text));
}
static inline int cetta_bytes_compare(const char *left, size_t left_len,
                                      const char *right, size_t right_len) {
    size_t common = left_len < right_len ? left_len : right_len;
    int order = common ? memcmp(left, right, common) : 0;
    if (order != 0)
        return order < 0 ? -1 : 1;
    return (left_len > right_len) - (left_len < right_len);
}
static inline int atom_string_compare(const Atom *left, const Atom *right) {
    return cetta_bytes_compare(left->ground.sval, left->ground.slen,
                               right->ground.sval, right->ground.slen);
}
Atom *atom_space(Arena *a, void *space_ptr);
/* How printed string literals write control bytes: escaped as \xhh, which
 * the HE readers read back (the default), or as the bytes themselves, which
 * the PeTTa and Prime readers read back.  The PeTTa printer always writes the
 * bytes; the program's language sets this for every other printer. */
void atom_print_set_raw_string_bytes(bool raw);
bool atom_print_raw_string_bytes(void);
/* While on, printing is into text held as a C string, which cannot hold NUL:
 * a printer that writes a string's bytes raw writes NUL as \x00.  Returns
 * the previous setting. */
bool atom_print_set_c_text(bool on);

/* State cell: holds a mutable value + its content type */
typedef struct {
    Atom *value;
    Atom *content_type; /* for (StateMonad τ) */
    uint64_t payload_owner_epoch; /* nonzero only for payload-local Rhometta scratch */
    uint64_t payload_export_owner_epoch; /* nonzero only for escaped owned exports */
} StateCell;

#if CETTA_PROVENANCE_ASSERT
void cetta_provenance_assert_not_transient(Atom *atom, const char *site);
void cetta_provenance_assert_not_transient_except(Atom *atom, const char *site,
                                                 const Arena *allowed_owner);
void cetta_provenance_assert_state_cell_not_transient(const StateCell *cell,
                                                     const char *site);
#else
static inline void cetta_provenance_assert_not_transient(Atom *atom,
                                                        const char *site) {
    (void)atom;
    (void)site;
}
static inline void cetta_provenance_assert_not_transient_except(
    Atom *atom, const char *site, const Arena *allowed_owner) {
    (void)atom;
    (void)site;
    (void)allowed_owner;
}
static inline void cetta_provenance_assert_state_cell_not_transient(
    const StateCell *cell, const char *site) {
    (void)cell;
    (void)site;
}
#endif

typedef struct {
    void *space_ptr;
    CettaEvaluatorOptions options;
} CaptureClosure;

/* Evaluator-minted identity for one Prime suspension.  There is deliberately
   no reader syntax for this grounded value. */
struct CettaPrimeNeedCapability {
    uint64_t session_id;
    uint64_t thunk_id;
    uint64_t authority_id;
    uint32_t rights;
};

/* Immutable semantic source of truth for a first-class Prime context.
   Newest frames occur first; extending a context allocates one frame and
   shares its parent.  Keys are closed structural names.  Values are closed
   atoms and may be evaluator-minted Need references while the context remains
   inside one evaluation episode.

   Algebra:
     lookup(bind(parent, key, value), key) = value
     other != key =>
       lookup(bind(parent, key, value), other) = lookup(parent, other)
     depth(bind(parent, key, value)) = depth(parent) + 1

   Extension never mutates parent.  Persistence must reify any private Need
   references before copying this chain into a space. */
struct CettaPrimeContext {
    const CettaPrimeContext *parent;
    Atom *key;
    Atom *value;
    uint32_t depth;
    uint32_t flags;
};

enum {
    CETTA_PRIME_NEED_RIGHT_FORCE = 1u << 0,
    CETTA_PRIME_NEED_RIGHT_INSPECT = 1u << 1,
    CETTA_PRIME_NEED_RIGHT_ALL =
        CETTA_PRIME_NEED_RIGHT_FORCE | CETTA_PRIME_NEED_RIGHT_INSPECT,
};

/* Rights are a bounded subset lattice over {force, inspect}.  Attenuation may
   select a subset of the current rights and never changes suspension identity;
   requesting a non-subset fails rather than restoring authority. */

Atom *atom_state(Arena *a, StateCell *cell);
Atom *atom_capture(Arena *a, CaptureClosure *closure);
/* A foreign record begins with its hold: an arena that holds an atom of the
 * record retains it once for that atom and releases it when the arena is
 * reset past the atom or freed, so the record lives exactly while some
 * arena holds an atom of it (ArenaHeldResources). */
typedef struct CettaForeignHold {
    void (*retain)(void *record);
    void (*release)(void *record);
    /* How PeTTa prints the record's value; NULL keeps the generic form. */
    void (*print_petta)(const void *record, FILE *out);
} CettaForeignHold;

Atom *atom_foreign(Arena *a, CettaForeignValue *value);

/* A node of a rational term's graph (term_graph.h), the payload of a
 * GV_TERM_GRAPH atom: one record per node for the graph's life, so two atoms
 * of one node hold the same pointer. */
typedef struct CettaTermGraph CettaTermGraph;
typedef struct {
    CettaTermGraph *graph;
    uint32_t node;
} CettaTermGraphRef;

/* The atom of a graph node, the graph retained by `arena`. */
Atom *atom_term_graph(Arena *arena, CettaTermGraph *graph, uint32_t node);
const CettaTermGraphRef *atom_term_graph_ref(const Atom *atom);
/* A reader of term structure sees a rational term's node, or its carrier,
 * as its term one level open (term_graph_open_value); any other atom is
 * itself.  NULL when the opening cannot be allocated. */
Atom *atom_rational_open(Arena *arena, Atom *atom);
static inline bool atom_is_rational_node(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_TERM_GRAPH;
}
/* A rational term's node as a value: its node atom when the node reaches no
 * free variable, else its carrier (term_graph.h). */
static inline bool atom_is_rational_value(const Atom *atom) {
    return atom_is_rational_node(atom) ||
           (atom && atom->kind == ATOM_EXPR && atom->expr.len >= 2u &&
            atom_is_internal_tag(atom->expr.elems[0],
                                 CETTA_INTERNAL_TAG_RATIONAL) &&
            atom_is_rational_node(atom->expr.elems[1]));
}
Atom *atom_internal_tag(Arena *a, CettaInternalTag tag);

/* A held value: syntax that a Data-typed position received as written.  The
   evaluator treats it as a value, syntax observers read its payload and
   return held fragments, explicit evaluation runs it, and it prints as its
   payload.  The representation is CETTA_INTERNAL_TAG_PRIME_HELD applied to
   the payload; wrapping a held value again is the identity, since a value
   that already has a Data type is passed as it is. */
bool atom_prime_held_is(const Atom *atom);
Atom *atom_prime_held_payload(Atom *atom);
Atom *atom_prime_held_wrap(Arena *a, Atom *atom);
/* Whether the process has made a held value yet: until it has, no term can
   contain one, and atom_contains_prime_held need not look. */
bool atom_prime_held_made(void);
/* Whether a held value occurs anywhere in `atom`. */
bool atom_contains_prime_held(const Atom *atom);
/* The syntax a value holds, at every depth: held wrappers removed.  For a
   position that takes syntax as such, a held value is that syntax. */
Atom *atom_prime_held_strip(Arena *a, Atom *atom);
Atom *atom_petta_prolog_compound(Arena *a, Atom *body);
/* PeTTa's no-result marker: an internal atom no program can write. */
Atom *atom_petta_no_result(Arena *a);
bool atom_petta_prolog_compound_body(Atom *atom, Atom **body);
bool atom_prolog_compound_body(Atom *atom, Atom **body);
Atom *atom_counted_collection(Arena *a, int64_t count);
/* True for grounded atoms whose value is their identity as a term: numbers,
 * strings and the list tags.  Handles, cells and machine carriers are not. */
bool atom_grounded_is_term_stable(const Atom *atom);
/* The list value [elems...]. */
Atom *atom_list(Arena *a, Atom *const *elems, CettaExprLen len);
/* [elems... | rest]: a list when rest is a list value (their concatenation),
 * otherwise the list pattern (LIST_REST elems... rest). */
Atom *atom_list_with_rest(Arena *a, Atom *const *elems, CettaExprLen len, Atom *rest);
/* The list of the elements of list from index from on. */
Atom *atom_list_tail(Arena *a, const Atom *list, CettaExprLen from);
/* A sequence of elems of the same kind as like: a list when like is one,
 * otherwise an expression. */
Atom *atom_sequence_like(Arena *a, const Atom *like, Atom *const *elems,
                         CettaExprLen len);
bool atom_counted_collection_count(
    Atom *atom, int64_t *count);
Atom *atom_prime_need_capability(Arena *a, uint64_t session_id,
                                 uint64_t thunk_id, uint64_t authority_id);
Atom *atom_prime_need_capability_with_rights(
    Arena *a, uint64_t session_id, uint64_t thunk_id,
    uint64_t authority_id, uint32_t rights);
const CettaPrimeNeedCapability *atom_prime_need_capability_value(
    const Atom *atom);
Atom *atom_prime_context_bind(Arena *a, const CettaPrimeContext *parent,
                              Atom *key, Atom *value);
const CettaPrimeContext *atom_prime_context_value(const Atom *atom);
Atom *atom_prime_context_lookup(const CettaPrimeContext *context, Atom *key);
uint32_t atom_prime_context_depth(const CettaPrimeContext *context);
Atom *atom_expr(Arena *a, Atom **elems, CettaExprLen len);
/* The part of an expression's summary a fixed head gives it, folded once:
 * atom_expr_headed then builds, from children headed by that head, exactly
 * the expression atom_expr builds. */
typedef struct {
    uint32_t flags;
    uint32_t structural_facts;
    /* The head is closed for the arena `arena_id` (for every arena when it
     * is 0), or for none when `open`. */
    uint32_t arena_id;
    bool open;
    /* The head changes the summary of what it heads (a name to resolve, a
     * native handle): atom_expr_headed builds through atom_expr. */
    bool adjusts;
} AtomExprHead;
void atom_expr_head_init(AtomExprHead *out, const Atom *head);
/* atom_expr of `elems`, whose first element is the head `head` was
 * initialized from. */
Atom *atom_expr_headed(Arena *a, const AtomExprHead *head, Atom **elems,
                       CettaExprLen len);
/* atom_expr_headed of `head_atom` and one, two or three arguments, passed
 * one by one. */
Atom *atom_expr_headed2(Arena *a, const AtomExprHead *head, Atom *head_atom,
                        Atom *first);
Atom *atom_expr_headed3(Arena *a, const AtomExprHead *head, Atom *head_atom,
                        Atom *first, Atom *second);
Atom *atom_expr_headed4(Arena *a, const AtomExprHead *head, Atom *head_atom,
                        Atom *first, Atom *second, Atom *third);
/* The suffix of `expression` from its child `offset` on.  It shares the
 * expression's children when their storage outlives the suffix: storage in
 * the suffix's own arena, allocated first, or storage never released.  Its
 * summary is the expression's when the departed children leave every summary
 * bit and the variables as the others fold them; otherwise it is folded from
 * its own children. */
Atom *atom_expr_suffix(Arena *a, Atom *expression, CettaExprLen offset);
/* A new header in arena `a`, of `view`'s length, over `elems`, which it
 * shares: `view`'s own elements, or a copy of them that equals them as
 * terms when `view` has no variables.  For a caller that keeps that storage,
 * and everything it names, for as long as `a` holds the header, as a
 * collection does for storage it does not collect or copies once.  The
 * summaries of the elements carry over; the ones relative to an arena are
 * cleared, which only withholds a shortcut.  NULL when `a` interns its
 * expressions, which share nothing they do not own. */
Atom *atom_expr_view_rehome(Arena *a, const Atom *view, Atom **elems);
/* The expression `list` with `head` before its first child, in amortized
 * constant time (PrefixBuffer).  When `list` starts at its buffer's front in
 * this arena, the free slot just below it is claimed; otherwise the children
 * are copied into a fresh buffer with as many free slots below them as they
 * are.  A claimed slot is never written again, so every expression sharing the
 * buffer keeps its children.  NULL when `list` is not an expression. */
Atom *atom_expr_prepend(Arena *a, Atom *head, Atom *list);
/* Single-allocation expression construction for incremental producers.
 * `begin` returns an unpublished draft whose child vector the caller fills;
 * `finish` computes all derived flags and returns the immutable expression
 * (or an existing hash-consed representative).  Apart from filling its child
 * vector, a draft must never escape or be consumed as an Atom before `finish`. */
Atom *atom_expr_builder_begin(Arena *a, CettaExprLen len);
Atom *atom_expr_builder_finish(Arena *a, Atom *draft);
/* Compatibility wrapper; immutable atoms are shared universally when the
   global hash-cons table is active. */
Atom *atom_expr_shared(Arena *a, Atom **elems, CettaExprLen len);
Atom *atom_expr2(Arena *a, Atom *a1, Atom *a2);
Atom *atom_expr3(Arena *a, Atom *a1, Atom *a2, Atom *a3);

/* ── Special atoms ──────────────────────────────────────────────────────── */

Atom *atom_empty(Arena *a);     /* Symbol "Empty" */
Atom *atom_unit(Arena *a);      /* Expression () — empty expr */
Atom *atom_true(Arena *a);      /* Symbol "True" */
Atom *atom_false(Arena *a);     /* Symbol "False" */

/* Error: (Error source message) */
Atom *atom_error(Arena *a, Atom *source, Atom *message);

/* ── Type system atoms (from HE spec Types.lean / Space.lean) ──────────── */

Atom *atom_undefined_type(Arena *a);   /* Symbol "%Undefined%" */
Atom *atom_atom_type(Arena *a);        /* Symbol "Atom" */
Atom *atom_symbol_type(Arena *a);      /* Symbol "Symbol" */
Atom *atom_variable_type(Arena *a);    /* Symbol "Variable" */
Atom *atom_expression_type(Arena *a);  /* Symbol "Expression" */
Atom *atom_grounded_type(Arena *a);    /* Symbol "Grounded" */
Atom *atom_list_type(Arena *a);        /* Symbol "List": metatype and type of a list */
Atom *get_meta_type(Arena *a, Atom *atom);  /* Meta-type of atom */
bool atom_is_meta_type(Atom *type);
bool atom_meta_type_accepts(Arena *a, Atom *formal, Atom *actual);

/* ── Predicates ─────────────────────────────────────────────────────────── */

/* `name` must have static storage duration: a string literal or a constant
 * table of literals.  The symbol it spells is cached by the pointer, so a
 * name in memory that is later reused for another spelling would be read as
 * the old one.  Use atom_is_symbol_named for any other string. */
bool atom_is_symbol(Atom *a, const char *name);
/* The same predicate for a name in arena, heap or stack memory: compares the
 * spelling, without the pointer cache. */
bool atom_is_symbol_named(Atom *a, const char *name);
bool atom_is_empty(Atom *a);
bool atom_is_error(Atom *a);
/* An integer, float, big integer or rational. */
bool atom_is_number(const Atom *atom);
bool atom_is_empty_or_error(Atom *a);
bool atom_is_var(Atom *a);
bool atom_is_expr(Atom *a);
bool atom_is_symbol_id(Atom *a, SymbolId id);
const char *atom_name_cstr(Atom *a);
SymbolId atom_head_symbol_id(const Atom *a);
static inline bool atom_has_identity_grounded(const Atom *atom) {
    return atom &&
           (atom->flags & ATOM_FLAG_HAS_IDENTITY_GROUNDED) != 0u;
}
static inline bool atom_has_thread_local_resource(const Atom *atom) {
    return atom &&
           (atom->flags & ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE) != 0u;
}

/* Stack-safe structural traversal for semantic predicates which cannot be
 * summarized in Atom flags.  The predicate is applied to every visited node
 * and traversal stops at its first true result. */
typedef bool (*AtomTreePredicate)(const Atom *atom, void *context);
bool atom_tree_any(const Atom *root, AtomTreePredicate predicate,
                   void *context);

/* ── Comparison ─────────────────────────────────────────────────────────── */

bool atom_eq(Atom *a, Atom *b);

/* Exact atom_eq comparison on finite, acyclic expression DAGs of symbols,
 * variables and scalar literals (integer, float, Boolean, string, bigint,
 * rational). Runtime handles and recursive grounded objects are not data
 * in this interface. Inputs must remain immutable for the call. Expression
 * sharing is not compared; scalar leaves use atom_eq (including its NaN
 * pointer-identity behavior). Cycles, unsupported leaves and traversal-size
 * overflow fail closed. No persistent cache. */
bool atom_data_equal(Atom *left, Atom *right);

/* Same comparison after both inputs have already been established as acyclic
 * scalar-data DAGs. Identical subgraphs can then be skipped without repeating
 * validation. This is not a validator for untrusted or mutable graph inputs. */
bool atom_data_equal_validated(Atom *left, Atom *right);
/* MeTTa's ==: a comparison that never binds.  Expressions compare element
 * by element, and a NaN equals nothing, even itself.
 * PeTTa: numbers are equal when their values are, whatever their
 * representation, so 1 equals 1.0 and 0.0 equals -0.0, while a float never
 * equals an integer it only rounds to.  Every other atom compares as a term.
 * HE: every other leaf compares as atom_eq does, so numbers keep the lane's
 * own equality, the one matching uses.  Other dialects use atom_eq. */
bool atom_value_eq(Atom *a, Atom *b);
/* A hash consistent with value equality in every lane: numbers hash by
 * value, so 1 and 1.0, and 1/2 and 0.5, hash alike.  atom_hash stays the
 * hash of the representation. */
uint32_t atom_value_hash(Atom *a);

/* A grounded leaf in caller storage, standing in for an unboxed Int, Float
 * or Bool so that it compares exactly as its atom would.  It belongs to no
 * arena and must not outlive the comparison or be published. */
void atom_scalar_leaf_int(Atom *out, int64_t value);
void atom_scalar_leaf_float(Atom *out, double value);
void atom_scalar_leaf_bool(Atom *out, bool value);

/* Upstream HE Number::PartialEq promotes an integer/float pair to float
 * and compares, so 2^53 + 1 equals 2^53 as a float.  he-compat does the
 * same; the other HE profiles compare the exact values.  PeTTa and every
 * other dialect stay kind-strict: 1 ≠ 1.0.  Same-kind pairs are not decided
 * here. */
bool cetta_he_promoted_kind_equal(int left_kind, int64_t left_int,
                                  double left_float, int right_kind,
                                  int64_t right_int, double right_float);
bool cetta_he_promoted_numbers_equal(const Atom *left, const Atom *right);
/* When value is an HE float that is an exact int64, *out receives it.
 * Candidate lookup uses this to visit the integer branch too. */
bool cetta_he_float_exact_int(double value, int64_t *out);

/* How an HE float query should read integer index branches.
 * NONE: not HE, or no int64 promotes to this float.
 * ONE: every promoting int64 is *out (magnitudes below 2^53).
 * SCAN: several int64 values share this float; visit each promoting key. */
typedef enum {
    CETTA_HE_FLOAT_INTS_NONE = 0,
    CETTA_HE_FLOAT_INTS_ONE,
    CETTA_HE_FLOAT_INTS_SCAN,
} CettaHeFloatIntBranches;

CettaHeFloatIntBranches cetta_he_float_int_branches(double value,
                                                    int64_t *out);

/* The canonical text of an HE float's exact value when that value is a
 * bigint or a rational: the key an index files such a number under.  NULL in
 * he-compat and other lanes, for an int64 value, and for a non-finite float.
 * The caller frees it. */
char *cetta_he_float_exact_key_text(double value);

/* ── Printing ───────────────────────────────────────────────────────────── */

void atom_print(Atom *a, FILE *out);
/* PeTTa's observable writer follows its SWI oracle: in particular it uses
   shortest round-tripping floats and does not double literal backslashes in
   string payloads. */
void atom_print_petta(Atom *a, FILE *out);
/* Print into arena-allocated string */
char *atom_to_string(Arena *a, Atom *atom);
char *atom_to_parseable_string(Arena *a, Atom *atom);
char *atom_to_parseable_string_petta(Arena *a, Atom *atom);
/* The printed bytes exactly, with their length: where a lane writes a
 * string's bytes raw, NUL included. */
char *atom_to_parseable_bytes(Arena *a, Atom *atom, bool petta,
                              size_t *len_out);

/* Deep-copy an atom DAG into a different arena, preserving source pointer
   sharing within one copy episode. */
Atom *atom_deep_copy(Arena *dst, Atom *src);
typedef Atom *(*AtomDeepCopyResolver)(void *context, Atom *src);
/* A multi-root copy episode.  Every call shares one source-pointer forwarding
   table, so pointer-DAG sharing is preserved across separately named roots.
   A destination-owned root is reused only when its compositional arena-closure
   flag proves that the full Atom-child graph is already safe.  An optional
   resolver, installed before copying, redirects every encountered node before
   traversal; update-cell collectors use it to collapse evaluated thunks. */
AtomDeepCopySession *atom_deep_copy_session_new(Arena *dst);
/* The session copies out of a region its caller releases right after it:
 * the atoms `arena` allocated since `mark`.  A copied atom of that region
 * then records its copy in place instead of in the session's table, since
 * nothing reads the region once the session ends.  False, leaving the table
 * in use, when the arena keeps more blocks below the mark than the session
 * tracks. */
bool atom_deep_copy_session_forward_region(AtomDeepCopySession *session,
                                           const Arena *arena,
                                           ArenaMark mark);
bool atom_deep_copy_session_retain_frame(
    AtomDeepCopySession *session, CettaFrameIdentity identity);
void atom_deep_copy_session_set_resolver(
    AtomDeepCopySession *session, AtomDeepCopyResolver resolver,
    void *context);
Atom *atom_deep_copy_session_copy(AtomDeepCopySession *session, Atom *src);
/* Return the destination image already established for `src` in this copy
   episode, or NULL without copying it.  Ephemeron traversal uses this query
   to discover keys reached from strong roots before conditionally tracing
   their values. */
Atom *atom_deep_copy_session_forwarded(
    const AtomDeepCopySession *session, const Atom *src);
/* True when copying `atom` in this session would return `atom` itself:
 * it is owned by the destination arena and closed for it. */
bool atom_deep_copy_session_settled(
    const AtomDeepCopySession *session, const Atom *atom);
void atom_deep_copy_session_free(AtomDeepCopySession *session);
/* Deep-copy with structural sharing for immutable atoms, also preserving source
   pointer sharing within one copy episode.
   Safe only for arenas whose contents outlive the global hash-cons table. */
Atom *atom_deep_copy_shared(Arena *dst, Atom *src);

static inline bool atom_has_vars(const Atom *atom) {
    return atom && (atom->flags & ATOM_FLAG_HAS_VARS) != 0;
}

static inline bool atom_has_registry_refs(const Atom *atom) {
    return atom && (atom->flags & ATOM_FLAG_HAS_REGISTRY_REFS) != 0;
}

static inline bool atom_has_private_variant_vars(const Atom *atom) {
    return atom &&
           (atom->flags & ATOM_FLAG_HAS_PRIVATE_VARIANT_VAR) != 0;
}

static inline bool cetta_expr_len_fits_u32(CettaExprLen len) {
    return len <= (CettaExprLen)UINT32_MAX;
}

static inline bool cetta_expr_len_fits_u16(CettaExprLen len) {
    return len <= (CettaExprLen)UINT16_MAX;
}

static inline bool cetta_expr_len_fits_size(CettaExprLen len) {
    return len <= (CettaExprLen)SIZE_MAX;
}

static inline bool cetta_expr_len_mul_fits_size(CettaExprLen len,
                                                size_t elem_size) {
    return elem_size == 0 ||
           len <= (CettaExprLen)(SIZE_MAX / elem_size);
}

#endif /* CETTA_ATOM_H */
