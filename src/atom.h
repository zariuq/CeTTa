#ifndef CETTA_ATOM_H
#define CETTA_ATOM_H

#include "storage_census.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "session.h"
#include "symbol.h"
#include "binding/frame_identity.h"

/* A negative result requires a completed comparison. Resource refusal or
 * an unsupported structural meeting supplies neither equality nor mismatch. */
typedef enum {
    CETTA_MATCH_INCOMPLETE,
    CETTA_MATCH_REFUTED,
    CETTA_MATCH_MATCHED,
} CettaMatchCompletion;

#ifndef CETTA_PROVENANCE_ASSERT
#define CETTA_PROVENANCE_ASSERT 0
#endif

#ifndef CETTA_MANAGED_SYMBOLS
#define CETTA_MANAGED_SYMBOLS 0
#endif

#ifndef CETTA_BUILD_WITH_GMP
#define CETTA_BUILD_WITH_GMP 0
#endif

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif

typedef struct CettaForeignValue CettaForeignValue;
typedef struct CettaDenseTensorV1 CettaDenseTensorV1;
typedef struct CettaBigInt CettaBigInt;
typedef struct CettaRational CettaRational;
typedef struct CettaPrimeNeedCapability CettaPrimeNeedCapability;
typedef struct CettaPrimeContext CettaPrimeContext;
/* A variable's identity: its stable source identity (`base`) and the frame
 * of the activation that renamed it (`epoch`; none outside activations).
 * Two 64-bit words with no lifetime cap.  A struct, so that identities are
 * compared with var_id_eq and never held in a narrower integer. */
typedef struct {
    uint64_t base;
    CettaFrameIdentity epoch;
} VarId;
typedef uint32_t NameId;
typedef uint64_t CettaExprLen;
typedef uint64_t CettaExprIndex;
typedef struct HashConsTable HashConsTable;
typedef struct ArenaFinalizer ArenaFinalizer;
typedef struct ArenaSymbolCache ArenaSymbolCache;
typedef struct AtomDeepCopySession AtomDeepCopySession;
/* Immutable bytes owned by the same arena allocation as their symbol Atom. */
typedef struct CettaSymbolSpelling {
    uint64_t hash;
    uint32_t len;
    char bytes[];
} CettaSymbolSpelling;

#define VAR_ID_NONE ((VarId){0u, {0u}})

static inline bool var_id_eq(VarId left, VarId right) {
    return left.base == right.base && left.epoch.word == right.epoch.word;
}

static inline bool var_id_is_none(VarId id) {
    return id.base == 0u && id.epoch.word == 0u;
}

/* The order of the former packed word: by epoch, then by base. */
static inline int var_id_cmp(VarId left, VarId right) {
    if (left.epoch.word != right.epoch.word)
        return left.epoch.word < right.epoch.word ? -1 : 1;
    return left.base < right.base ? -1 : left.base > right.base ? 1 : 0;
}

static inline bool var_id_lt(VarId left, VarId right) {
    return var_id_cmp(left, right) < 0;
}

/* The identity a source variable has outside every activation. */
static inline VarId var_id_from_base(uint64_t base) {
    return (VarId){base, {0u}};
}

/* A variable's source identity, its activation dropped. */
static inline VarId var_source_id(VarId id) {
    return var_id_from_base(id.base);
}

/* Both words mixed into one, for tables keyed by identity. */
static inline uint64_t var_id_mix(VarId id) {
    uint64_t mixed = id.base ^ (id.epoch.word * UINT64_C(0x9e3779b97f4a7c15));
    mixed ^= mixed >> 33u;
    mixed *= UINT64_C(0xff51afd7ed558ccd);
    mixed ^= mixed >> 29u;
    return mixed;
}
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
    GV_TERM_GRAPH,
    GV_TENSOR,
    /* An opaque authority object of the shared seal substrate (seal.h): a
     * brand facet, a capsule or a key.  Its only identity is the object
     * itself; no printed form, number or payload reconstructs one. */
    GV_SEAL,
    /* Immutable library values with representation-independent equality. */
    GV_STRUCTURAL
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
    /* A braces node {x1 ... xn} of the Prime reader, the set notation of
     * 9/27, is the expression (BRACES x1 ... xn).  No set form is defined
     * for it yet, so it is inert data: never called, its elements never
     * evaluated, and it equals or matches only a braces node with equal
     * elements in order.  Inside `lam`, before the parameter, the
     * elaboration reads it as the names the lambda shares. */
    CETTA_INTERNAL_TAG_PRIME_BRACES = 14,
    /* The own list of an elaborated Prime lambda or template,
     * (OWN RECORD $y ...), last in it (atom_is_prime_own_list): inferred
     * ownership, metadata no observer of the authored term sees. */
    CETTA_INTERNAL_TAG_PRIME_OWN = 15,
    /* A binder of quoted code by its level, (CODE_BINDER level), standing
     * for the binder and its references while a quoted pattern is matched
     * against quoted code (prime_semantics_code_canonical): binders meet by
     * position, never by spelling.  It exists only during that match. */
    CETTA_INTERNAL_TAG_PRIME_CODE_BINDER = 16,
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
           cetta_internal_tag_is_callable(tag) ||
           tag == (int64_t)CETTA_INTERNAL_TAG_PRIME_BRACES ||
           tag == (int64_t)CETTA_INTERNAL_TAG_PRIME_OWN;
}

#define ATOM_FLAG_HAS_VARS 0x01u
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
#define ATOM_FLAG_VAR_BLOOM_WIDTH 22u
#define ATOM_FLAG_VAR_BLOOM_MASK UINT32_C(0x7FFFFE00)
/*
 * The evaluated mark: this expression is a value, and evaluating it again
 * answers it unchanged.  HE marks the answer its evaluation gives (Hyperon
 * Experimental's metta), at that answer's root.  Prime marks a completed
 * value, a datum its producer completed (a native operation's completed
 * value, the datum of `return`), at every expression of that datum: the
 * parts a pattern takes from it are values in turn, and a Need cell inside
 * it is still a computation (prime_completed_value in eval.c).  The mark
 * belongs to one occurrence of a value, not to the term: an expression never
 * inherits it from a child, an interned node never carries it, equality and
 * hashing ignore it, a copy of the value keeps it, and a space storing the
 * atom drops it, since a space holds syntax.
 */
#define ATOM_FLAG_HE_EVALUATED UINT32_C(0x80000000)
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
#define ATOM_STRUCTURAL_HAS_SPELLED_SYMBOL UINT32_C(0x00000400)
/* Every reachable Atom allocation is global or in this atom's arena, and
 * every external leaf is a native-handle holder retained by that arena.
 * This is a conjunction over children, not an inherited occurrence bit.
 * It certifies carrier lifetime only: it grants neither a shallow-copy or
 * hash-cons licence nor permission to use a closed registry resource. */
#define ATOM_STRUCTURAL_OWNER_CLOSED UINT32_C(0x00000800)
/* A string leaf carries a retained immutable byte owner after its Atom.
 * This is a representation fact of that leaf alone, never an inherited
 * occurrence bit. String equality, hashing and printing still use bytes. */
#define ATOM_STRUCTURAL_STRING_STORAGE UINT32_C(0x00001000)
/* The atom or one of its descendants carries HE's evaluated mark
 * (ATOM_FLAG_HE_EVALUATED).  A marked node sets it for itself; an expression
 * has it when a child has it.  Storage that drops the mark copies such an
 * atom rather than sharing it. */
#define ATOM_STRUCTURAL_HAS_HE_EVALUATED UINT32_C(0x00002000)
/* Every leaf of the atom is a symbol, so its structural hash is a function of
 * what atom_eq compares: two such atoms whose known hashes differ are
 * unequal.  Like the closed facts, it holds of an expression only when it
 * holds of every child; any other leaf, or a hand-built atom, leaves it
 * unset. */
#define ATOM_STRUCTURAL_SYMBOLIC UINT32_C(0x00004000)
/* Each fact has its own bit: for one-bit flags the sum equals the union
 * exactly when no two share one. */
_Static_assert(ATOM_STRUCTURAL_FACTS_VALID + ATOM_STRUCTURAL_HAS_INTERNAL_TAG +
                       ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID + ATOM_STRUCTURAL_HAS_NAN +
                       ATOM_STRUCTURAL_HAS_OPEN_LIST + ATOM_STRUCTURAL_HAS_LIST +
                       ATOM_STRUCTURAL_GENERATION_CLOSED +
                       ATOM_STRUCTURAL_HAS_LIST_CARRIER + ATOM_STRUCTURAL_FRONT_SLACK +
                       ATOM_STRUCTURAL_HAS_RATIONAL +
                       ATOM_STRUCTURAL_HAS_PETTA_NONLIST +
                       ATOM_STRUCTURAL_HAS_SPELLED_SYMBOL +
                       ATOM_STRUCTURAL_OWNER_CLOSED + ATOM_STRUCTURAL_STRING_STORAGE +
                       ATOM_STRUCTURAL_HAS_HE_EVALUATED + ATOM_STRUCTURAL_SYMBOLIC ==
                   (ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_HAS_INTERNAL_TAG |
                    ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID | ATOM_STRUCTURAL_HAS_NAN |
                    ATOM_STRUCTURAL_HAS_OPEN_LIST | ATOM_STRUCTURAL_HAS_LIST |
                    ATOM_STRUCTURAL_GENERATION_CLOSED |
                    ATOM_STRUCTURAL_HAS_LIST_CARRIER | ATOM_STRUCTURAL_FRONT_SLACK |
                    ATOM_STRUCTURAL_HAS_RATIONAL |
                    ATOM_STRUCTURAL_HAS_PETTA_NONLIST |
                    ATOM_STRUCTURAL_HAS_SPELLED_SYMBOL |
                    ATOM_STRUCTURAL_OWNER_CLOSED | ATOM_STRUCTURAL_STRING_STORAGE |
                    ATOM_STRUCTURAL_HAS_HE_EVALUATED | ATOM_STRUCTURAL_SYMBOLIC),
               "structural fact bits overlap");

/*
 * VariantShape reserves this epoch for its runtime-private slots, whose
 * ordinal is the base: handle 0xA11A at generation zero, which the frame
 * identity facility never issues (generations count from one).  Keeping the
 * namespace test beside VarId lets immutable atoms summarize the property
 * compositionally instead of rescanning an arbitrarily deep value at every
 * Bindings boundary.
 */
#define CETTA_VARIANT_PRIVATE_SLOT_EPOCH ((CettaFrameIdentity){UINT64_C(0xA11A)})

static inline bool atom_var_id_is_private_variant(VarId id) {
    return id.epoch.word == CETTA_VARIANT_PRIVATE_SLOT_EPOCH.word;
}

/* ── Atom ───────────────────────────────────────────────────────────────── */

/* The arena id of an atom whose copy it records; never an arena's. */
#define ATOM_ARENA_ID_FORWARDED UINT32_MAX

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
    SymbolId symbol_number; /* Raw ID: access Symbol values through the API below. */
    /*
     * Allocation identity, not part of atom equality or hashing.  Arena
     * identities make ownership tests for known Atom pointers O(1), avoiding
     * a scan of every arena block during graph evacuation.  Zero denotes an
     * atom not owned by an Arena (for example a hash-consed global).
     */
    uint32_t arena_id;
    union {
        Atom *name_key;      /* Variable name, or evacuation forwarding pointer. */
        const CettaSymbolSpelling *spelling; /* Unnumbered ATOM_SYMBOL only. */
    };
    /* The structural hash, memoized: 0 until it is known, and never 0 after
     * (a computed 0 is spelled as a fixed nonzero word).  The one field
     * written after publication, by any thread that hashes the atom, so it
     * is only read and written atomically (atom_hash_cache_load). */
    uint32_t hash_cache;
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

static inline SymbolId atom_symbol_current_id(const Atom *atom);

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

/* A quotation `(quote X)`.  In Prime a quotation in value position is
 * sealed code: substitution never enters it.  This is the one definition of
 * the seal.  Lambda's beta and `lift let` (prime_semantics.c) always stop
 * here; the canonical binders (abt.c) and the environment action stop here
 * while the session seals (prime_quote_seal_active), and authored forms are
 * sealed when they are elaborated (prime_semantics_elaborate_form). */
static inline bool atom_sequence_is_own_list(const Atom *atom);
/* A written Prime quotation is a scope: the names it owns are its own list,
 * `(quote X (OWN RECORD $u ...))`, last in it, hidden metadata as a
 * template's own list is (atom_prime_template_own_hidden); each opening of
 * the code copies them. */
static inline bool atom_is_quotation(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR &&
           (atom->expr.len == 2u ||
            (atom->expr.len == 3u && atom->expr.elems[2] &&
             atom->expr.elems[2]->kind == ATOM_EXPR &&
             atom_sequence_is_own_list(atom->expr.elems[2]))) &&
           atom->expr.elems[0] &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           atom_symbol_current_id(atom->expr.elems[0]) == g_builtin_syms.quote;
}

/* The drop `*@$x` of a quoted variable, `(unquote (quote $x))`.  The drop of
 * a quoted name refers to the name's binder, as `*@x` does for lam
 * (prime_binder_reference), so the seal does not cover its operand. */
static inline bool atom_is_drop_of_quoted_variable(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 2u &&
           atom->expr.elems[0] &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           atom_symbol_current_id(atom->expr.elems[0]) == g_builtin_syms.unquote &&
           atom_is_quotation(atom->expr.elems[1]) &&
           atom->expr.elems[1]->expr.elems[1] &&
           atom->expr.elems[1]->expr.elems[1]->kind == ATOM_VAR;
}

/* The own-name list of an elaborated Prime lambda or template,
 * `(OWN RECORD $y ...)`, last in it.  RECORD is the symbol
 * `ownership/lifetime/readout` of the scope profile the template was
 * elaborated under; the variables after it are the slots each activation
 * copies.  Its head is an internal tag no source text spells, so the list
 * cannot be forged by writing.  Inferred ownership is hidden metadata: the
 * printer, the sequence operations, patterns and `==` see the authored term
 * only (atom_prime_template_own_hidden). */
bool atom_is_prime_own_list(const Atom *atom);

/* A braces node {x1 ... xn}, (BRACES x1 ... xn). */
static inline bool atom_is_prime_braces(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len >= 1u &&
           atom_is_internal_tag(atom->expr.elems[0],
                                CETTA_INTERNAL_TAG_PRIME_BRACES);
}

/* A meta-argument wrapper (meta T [...]) or (meta T {...}): what the Prime
 * reader makes of a bracket list or a braces node touching a term's end,
 * T[...] or T{...}.  The printer writes it back touching. */
static inline bool atom_is_prime_meta(const Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom->expr.elems[0] || atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom_symbol_current_id(atom->expr.elems[0]) != g_builtin_syms.prime_meta)
        return false;
    const Atom *argument = atom->expr.elems[2];
    return atom_is_prime_braces(argument) ||
           (argument && argument->kind == ATOM_EXPR &&
            argument->expr.len >= 1u &&
            atom_is_internal_tag(argument->expr.elems[0],
                                 CETTA_INTERNAL_TAG_LIST));
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

/* Decode the bounded constructor coordinates that determine a value's
 * public role. Readers with a lexical environment resolve these coordinates
 * before using the same decoder as a materialized atom. */
static inline PeTTaValueRepresentation atom_petta_expression_representation(
        CettaExprLen length, const Atom *head,
        CettaExprLen domain_length, const Atom *domain_head) {
    if (length == 2u && atom_is_internal_tag(
            head, CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND))
        return PETTA_VALUE_COMPOUND;
    if (length == 3u) {
        if (atom_is_internal_tag(head, CETTA_INTERNAL_TAG_PETTA_PARTIAL))
            return PETTA_VALUE_COMPOUND;
        if (atom_is_internal_tag(head, CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE) ||
            (domain_length == 2u && atom_is_internal_tag(
                domain_head, CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY)))
            return PETTA_VALUE_REGISTERED_CALLABLE;
    }
    return PETTA_VALUE_ORDINARY;
}

static inline PeTTaValueRepresentation atom_petta_value_representation(
        const Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR ||
        ((atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u &&
         (atom->structural_facts & ATOM_STRUCTURAL_HAS_PETTA_NONLIST) == 0u))
        return PETTA_VALUE_ORDINARY;
    const Atom *domain = atom->expr.len == 3u ? atom->expr.elems[1] : NULL;
    bool domain_pair = domain && domain->kind == ATOM_EXPR && domain->expr.len == 2u;
    return atom_petta_expression_representation(
        atom->expr.len, atom->expr.len ? atom->expr.elems[0] : NULL,
        domain_pair ? 2u : 0u, domain_pair ? domain->expr.elems[0] : NULL);
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
/* Whether `atom`, an expression, is a Prime own list `(OWN RECORD $y ...)`
 * (atom_is_prime_own_list, read here by its head alone). */
static inline bool atom_sequence_is_own_list(const Atom *atom) {
    return atom->expr.len >= 2u &&
           atom_is_internal_tag(atom->expr.elems[0],
                                CETTA_INTERNAL_TAG_PRIME_OWN);
}

/* The shapes whose final private OWN list describes a binding scope.
 * Shared by pointer atoms and stored term IDs, so indexing and matching
 * observe the same authored arity. Three fields also cover let* pairs. */
static inline bool atom_prime_scope_metadata_shape(CettaExprLen len, SymbolId head) {
    return len == 3u ||
           (len == 4u && head == g_builtin_syms.prime_lam) ||
           (len == 5u && head == g_builtin_syms.map_atom) ||
           (len == 7u && head == g_builtin_syms.foldl_atom) ||
           (len == 6u && head == g_builtin_syms.match) ||
           (len == 5u && (head == g_builtin_syms.let ||
                          head == g_builtin_syms.match ||
                          head == g_builtin_syms.chain ||
                          head == g_builtin_syms.filter_atom)) ||
           ((len == 4u || len == 5u) &&
            (head == g_builtin_syms.case_text ||
             head == g_builtin_syms.switch_text ||
             head == g_builtin_syms.switch_minimal));
}

/* Inferred ownership is hidden from observers of the authored syntax.
 * An expression that holds no internal tag is answered by one fact bit. */
static inline bool atom_prime_template_own_hidden(const Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR ||
        (atom->structural_facts &
         (ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_HAS_INTERNAL_TAG)) ==
            ATOM_STRUCTURAL_FACTS_VALID)
        return false;
    CettaExprLen len = atom->expr.len;
    if (len < 3u) return false;
    const Atom *last = atom->expr.elems[len - 1u];
    if (!last || last->kind != ATOM_EXPR || !atom_sequence_is_own_list(last))
        return false;
    SymbolId head = atom->expr.elems[0] && atom->expr.elems[0]->kind == ATOM_SYMBOL
        ? atom_symbol_current_id(atom->expr.elems[0]) : SYMBOL_ID_NONE;
    return atom_prime_scope_metadata_shape(len, head);
}

/* The length of an expression as the authored term has it: an elaborated
 * template's own list left out. */
static inline CettaExprLen atom_authored_len(const Atom *atom) {
    return atom->expr.len - (atom_prime_template_own_hidden(atom) ? 1u : 0u);
}

static inline bool atom_sequence_view(const Atom *atom, Atom *const **elems,
                                      CettaExprLen *len) {
    /* A braces node is no sequence: no order is defined on it. */
    if (!atom || atom->kind != ATOM_EXPR || atom_is_list_rest(atom) ||
        atom_petta_value_representation(atom) != PETTA_VALUE_ORDINARY ||
        atom_is_prime_braces(atom))
        return false;
    bool list = atom_is_list(atom);
    *elems = atom->expr.elems + (list ? 1u : 0u);
    *len = atom->expr.len - (list ? 1u : 0u);
    /* An elaborated template's own list is not part of the authored term
     * a sequence operation reads (atom_print does not show it either). */
    if (!list && atom_prime_template_own_hidden(atom))
        *len = atom->expr.len - 1u;
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
    if (var_id_is_none(id))
        return 0u;
    uint64_t mixed = var_id_mix(id);
    uint32_t first =
        (uint32_t)(mixed % ATOM_FLAG_VAR_BLOOM_WIDTH);
    uint32_t second =
        (uint32_t)((mixed >> 32u) % ATOM_FLAG_VAR_BLOOM_WIDTH);
    return (UINT32_C(1) << (ATOM_FLAG_VAR_BLOOM_SHIFT + first)) |
           (UINT32_C(1) << (ATOM_FLAG_VAR_BLOOM_SHIFT + second));
}

/* Threads that hash one atom race to store the same word, so relaxed
 * order suffices: the hash is a function of the immutable fields. */
static inline uint32_t atom_hash_cache_load(const Atom *atom) {
    return __atomic_load_n(&atom->hash_cache, __ATOMIC_RELAXED);
}

/* Copy an atom's header, reading its hash cache as the atomic word it is. */
static inline void atom_header_copy(Atom *dst, const Atom *src) {
    size_t after = offsetof(Atom, hash_cache) + sizeof(src->hash_cache);
    memcpy(dst, src, offsetof(Atom, hash_cache));
    dst->hash_cache = atom_hash_cache_load(src);
    memcpy((char *)dst + after, (const char *)src + after,
           sizeof(Atom) - after);
}

static inline bool atom_is_he_evaluated(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR &&
           (atom->flags & ATOM_FLAG_HE_EVALUATED) != 0u;
}

/* Whether `atom` may hold an evaluated mark: its facts say so, or an
 * expression's facts are not known. */
static inline bool atom_may_hold_he_mark(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR &&
           ((atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) == 0u ||
            (atom->structural_facts & ATOM_STRUCTURAL_HAS_HE_EVALUATED) != 0u);
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

/* The resource-ledger category an arena's blocks are charged to under
 * runtime statistics.  It follows the runtime kind unless an owner names a
 * category (cursor working storage, space storage); NONE marks an arena
 * created while statistics were off, which is never charged.  Diagnostic
 * only: no category changes allocation or evaluation. */
typedef enum {
    CETTA_ARENA_LEDGER_NONE = 0,
    CETTA_ARENA_LEDGER_OTHER = 1,
    CETTA_ARENA_LEDGER_PERSISTENT = 2,
    CETTA_ARENA_LEDGER_EVAL = 3,
    CETTA_ARENA_LEDGER_SCRATCH = 4,
    CETTA_ARENA_LEDGER_SURVIVOR = 5,
    CETTA_ARENA_LEDGER_CURSOR = 6,
    CETTA_ARENA_LEDGER_SPACE = 7,
} CettaArenaLedgerTag;

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t capacity;
    size_t used;
    char data[ARENA_BLOCK_SIZE];
} ArenaBlock;

typedef struct ArenaRetainedOwnerSet ArenaRetainedOwnerSet;
typedef struct ArenaFrameIdentitySet ArenaFrameIdentitySet;

typedef struct Arena {
    ArenaBlock *head;
    ArenaBlock *spare;
    HashConsTable *hashcons;
    ArenaSymbolCache *symbol_cache;
    /* Metadata retained across partial reset: scalar caches and identity ownership. */
    size_t persistent_bytes;
    size_t live_bytes;
    uint64_t managed_symbol_count;
    uint64_t managed_symbol_bytes;
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
    /* Owners this arena holds a reference to, one per distinct (owner,
     * release) pair, in acquisition order with a hash index.  Each hold
     * is also a finalizer, so a reset releases exactly the holds
     * acquired after its mark. */
    ArenaRetainedOwnerSet *retained_owners;
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
    /* Copies into this arena make symbols of their own instead of taking
     * the symbol table's one atom for each.  Opt-in. */
    bool own_symbols;
    /* Retired full-size blocks go to the thread's stock for its next
     * recycling arena instead of back to the allocator.  Opt-in, for owners
     * that create and retire many short-lived arenas. */
    bool recycle_blocks;
    /* block_capacity is the first block's size; a later block matches what
     * the arena already holds (arena_set_block_growth). */
    bool block_growth;
    /* CettaArenaLedgerTag charged for this arena's blocks, and whether an
     * owner named it (otherwise it follows the runtime kind). */
    uint8_t ledger_tag;
    bool ledger_tag_named;
} Arena;

typedef struct {
    ArenaBlock *head;
    size_t used;
    size_t live_bytes;
    uint64_t managed_symbol_count;
    uint64_t managed_symbol_bytes;
    size_t external_bytes;
    size_t persistent_bytes;
    size_t reserved_bytes;
    uint32_t block_count;
    ArenaFinalizer *finalizers;
    /* Distinct frame identities this arena owned at the mark.  A reset keeps
     * exactly this many holds, so ownership acquired after the mark is
     * released and ownership acquired before it stays valid. */
    uint32_t frame_identity_len;
} ArenaMark;

/* An immutable saved substitution. The binding layer owns its representation
 * and every syntax/frame lifetime it uses, independently of the source arena;
 * atom copying only retains the owner, and printing asks for a projection.
 * Ordinary syntax substitution cannot traverse or rewrite its domain. */
typedef struct CettaBindingsValue {
    void (*retain)(void *);
    void (*release)(void *);
    Atom *(*observe)(Arena *, const struct CettaBindingsValue *);
    bool (*equal)(const struct CettaBindingsValue *,
                  const struct CettaBindingsValue *);
    CettaStorageVisitor storage;
    Atom *(*apply)(Arena *, const struct CettaBindingsValue *, Atom *);
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
void arena_storage_census(const Arena *arena, CettaStorageCensus *census);
/* The first block has `first` bytes, and each later block claimed from the
 * allocator is as large as all the blocks the arena holds, up to
 * ARENA_BLOCK_SIZE: what an arena holds grows geometrically with what it
 * allocates, and an arena that gives back every block starts small again.
 * An allocation larger than that still gets a block of its own size. */
void  arena_set_block_growth(Arena *a, size_t first);
void  arena_free(Arena *a);
void  arena_reserve(Arena *a, size_t size);
void  arena_set_hashcons(Arena *a, HashConsTable *hc);
void  arena_set_runtime_kind(Arena *a, CettaArenaRuntimeKind kind);
/* Charge this arena's blocks to `tag` in the resource ledger from now on,
 * moving what it already holds there. */
void  arena_set_ledger_tag(Arena *a, CettaArenaLedgerTag tag);
void  arena_set_block_recycling(Arena *a, bool enabled);
/* Name `older` as the older generation of `young`, whose atoms may then share
 * its storage.  The owner releases `older` only together with `young`.  An
 * arena that has an older generation cannot serve as one: the link is then
 * left unset. */
void  arena_set_older_generation(Arena *young, const Arena *older);
/* Share one atom per symbol and per small integer within each reset epoch of
 * this arena.  Atoms are immutable, so sharing is invisible to observers;
 * a reset or free forgets every shared atom with the storage it lived in. */
void  arena_set_scalar_cache(Arena *a, bool enabled);
/* Copies into this arena make symbols of their own, where they would take
 * the symbol table's one atom for each. */
void  arena_set_own_symbols(Arena *a, bool enabled);
/* Keep a contextual identity alive until this arena's corresponding reset.
 * Ambient/external identifiers have no recyclable owner to retain. */
bool arena_retain_frame_identity(Arena *a, CettaFrameIdentity identity);
#ifdef CETTA_TEST_HOOKS
/* Ownership observations for the arena frame-identity lifetime tests. */
uint32_t arena_frame_identity_count_test(const Arena *a);
uint32_t arena_frame_identity_capacity_test(const Arena *a);
/* Mint the next fresh arena name at `next` (a test starting near the end of
 * the name space), and count releases of names nobody owned. */
void arena_identity_test_start_fresh_at(uint32_t next);
uint64_t arena_identity_test_stale_releases(void);
bool arena_frame_identity_owned_test(const Arena *a,
                                     CettaFrameIdentity identity);
#endif
/* Retain an immutable external owner once per arena lifetime segment. A reset
 * releases owners first retained after its mark; earlier owners remain live.
 * Ordinary atom copies must still transport their payload into the destination. */
bool arena_retain_owner_with_storage(Arena *a, void *owner,
    void (*retain)(void *), void (*release)(void *), CettaStorageVisitor storage);
bool arena_retain_owner(Arena *a, void *owner,
                        void (*retain)(void *), void (*release)(void *));
/* Read-only support for an optional bounded owner.  The caller owns the
 * arena's mutation authority while checking and subsequently retaining. */
bool arena_retains_owner(const Arena *a, const void *owner,
                         void (*release)(void *));
/* Borrow the indexed owner among actual retained edges with this release
 * operation. Exclusive arena access and the original lifetime hold remain
 * required; enumeration does not retain or synchronize the owner. */
const void *arena_retained_owner_at(const Arena *arena,
    void (*release)(void *), size_t index);
size_t arena_retained_owner_allocation_bytes(void);

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
    if (live > SIZE_MAX - a->persistent_bytes)
        return SIZE_MAX;
    return live + a->persistent_bytes;
}

inline size_t arena_mark_accounted_live_bytes(ArenaMark mark) {
    size_t live = mark.live_bytes;
    if (live > SIZE_MAX - mark.external_bytes)
        return SIZE_MAX;
    live += mark.external_bytes;
    if (live > SIZE_MAX - mark.persistent_bytes)
        return SIZE_MAX;
    return live + mark.persistent_bytes;
}

void  arena_account_external_bytes(Arena *a, size_t size);
/* Native handle identifiers retain an external owner without changing their
 * integer equality, hash, or printed representation. Only these identifiers
 * have the extended allocation; ordinary Atom layout is unchanged. */
Atom *atom_native_handle_identifier_with_storage(Arena *a, int64_t id, void *owner,
    void (*retain)(void *), void (*release)(void *), CettaStorageVisitor storage);
/* Preserve a held identifier's authentication tuple while a distinct
 * owned carrier supplies its payload lifetime. This does not issue an id,
 * renew its slot, or grant resource mutation. The source must remain live
 * through construction and the callbacks own the carrier's lifetime. */
Atom *atom_native_handle_identifier_with_carrier(Arena *a, const Atom *source,
    void *lifetime, void (*retain)(void *), void (*release)(void *),
    CettaStorageVisitor storage);
Atom *atom_native_handle_identifier(Arena *a, int64_t id, void *owner,
                                    void (*retain)(void *),
                                    void (*release)(void *));
/* Test the actual retained holder, not integer equality or printed syntax. */
bool atom_native_handle_identifier_has_owner(const Atom *identifier, const void *owner);
/* A conservative transitive payload footprint, charged once per arena owner.
 * The callback must not allocate or re-enter that arena. Copies keep it. */
Atom *atom_native_handle_identifier_tracked_with_storage(Arena *a, int64_t id,
    void *owner, void (*retain)(void *), void (*release)(void *),
    CettaStorageVisitor storage, size_t (*footprint)(void *));
Atom *atom_native_handle_identifier_tracked(Arena *a, int64_t id, void *owner,
    void (*retain)(void *), void (*release)(void *), size_t (*footprint)(void *));
/* Compare the retained authority, not just the printed integer. */
bool atom_native_handle_identifier_matches(const Atom *atom, const void *owner,
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
/* A retained-resource carrier has a lifetime certificate distinct from the
 * stronger arena-closed certificate consumed by shallow-copy fast paths. */
static inline bool atom_graph_has_owner_closure(const Atom *atom) {
    const uint32_t owned = ATOM_STRUCTURAL_FACTS_VALID |
                           ATOM_STRUCTURAL_OWNER_CLOSED;
    return atom && ((atom->flags & ATOM_FLAG_ARENA_CLOSED) != 0u ||
                   (atom->structural_facts & owned) == owned);
}
bool  atom_graph_is_owner_closed_for_arena(const Arena *arena,
                                           const Atom *atom);
/* Whether a copy into `arena` may share `atom` as it is: the atom is closed
 * in `arena`, or in `arena`'s older generation, across the generations it
 * reaches.  Globally owned atoms are not shared this way. */
bool  atom_settled_for_arena(const Arena *arena, const Atom *atom);
/* Exact bounds of arena identities in the Atom carrier. Arena-retained native
 * handle control cells have independent lifetime; other external references
 * make these bounds incomplete. This is not a resource-payload census. */
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
    /* Nonzero for one live constructor incarnation; never a pointer lease. */
    uint64_t instance_id;
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
void hashcons_storage_census(const HashConsTable *hc, CettaStorageCensus *census);
/* Process-wide stamp for caches borrowing atoms from any hash-cons table.
 * Reclamation advances it before freeing atoms; UINT64_MAX permanently
 * disables pointer-key reuse rather than wrapping. The caller still keeps
 * every borrowed atom alive throughout each operation that reads it. */
uint64_t hashcons_reclamation_epoch(void);
/* The caller holds the table header alive. Zero means retired or identity
 * exhaustion. Other tables' retirement and this table's growth leave its
 * instance unchanged; free followed by init obtains a new instance. */
uint64_t hashcons_instance_id(const HashConsTable *hc);
/* Allocated bucket and scalar-cache arrays; excludes atom payloads and
 * frame-identity storage. Query only in the owning worker. */
uint64_t hashcons_index_reserved_bytes(const HashConsTable *hc);
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

/* Concurrent readers may use hashes published before their episode, but must
 * not mutate the lazy cache on a shared Atom or any recursively hashed child.
 * These nested, thread-local scopes require quiescent publication of the
 * borrowed graph; they do not synchronize an unscoped writer. */
void atom_hash_read_scope_enter(void);
void atom_hash_read_scope_leave(void);

/* A spread hash for known identity-free finite structure or certified
 * infinite unfoldings. False leaves out unchanged when neither domain is
 * established. A collection must switch
 * to canonical equality when any key declines: rational views can compare
 * equal to finite trees while having a different physical representation.
 * Shared expression children are memoized within this call. */
bool atom_spread_hash(Atom *atom, uint64_t *out);

/* A hash cache of 0 means "not yet known", so every structural hash, cached
 * or not, spells a computed 0 as this word: leaves, expression folds, the
 * compile-time list hashes and the term universe's hash32 alike, so a cached
 * child and a recomputed one contribute alike and an atom's hash equals its
 * universe record's. */
#define ATOM_HASH_ZERO_SPELLING UINT32_C(0x9e3779b9)
#define ATOM_HASH_SPELL(h) \
    ((uint32_t)(h) != 0u ? (uint32_t)(h) : ATOM_HASH_ZERO_SPELLING)

/* An expression's hash is finished once its children are folded in, before
 * it is spelled.  The fold xors its last child in, so without the finish a
 * constructor applied twice would give back its argument's hash ((S (S x))
 * hashing as x) and whole families of nested terms would share two hashes.
 * The finish is murmur3's fmix32, a bijection taking 0 to 0; every place
 * that folds an expression's hash (atom.c, the term universe, the
 * compile-time list hashes) finishes it alike. */
#define ATOM_HASH_FINISH_STEP(h, shift, factor) \
    ((uint32_t)(((uint32_t)(h) ^ ((uint32_t)(h) >> (shift))) * (factor)))
#define ATOM_HASH_EXPR_FINISH(h)                                          \
    ((uint32_t)(ATOM_HASH_FINISH_STEP(                                    \
                    ATOM_HASH_FINISH_STEP((h), 16, 0x85ebca6bu), 13,      \
                    0xc2b2ae35u) ^                                        \
                (ATOM_HASH_FINISH_STEP(                                   \
                     ATOM_HASH_FINISH_STEP((h), 16, 0x85ebca6bu), 13,     \
                     0xc2b2ae35u) >> 16)))

static inline uint32_t atom_hash_expr_finish(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

/* ── Variable identity intern/freshening ──────────────────────────────── */

/* An interned source variable has no activation epoch, so an entry holds
 * its base alone (zero until interned): one lock-free word per spelling. */
typedef _Atomic(uint64_t) VarInternEntry;

typedef struct {
    _Atomic(VarInternEntry *) *chunks;
    pthread_mutex_t write_mutex;
} VarInternTable;

void    var_intern_init(VarInternTable *t);
void    var_intern_free(VarInternTable *t);
VarId   var_intern(VarInternTable *t, SymbolId spelling);
bool    fresh_var_id_try(VarId *id_out);
VarId   fresh_var_id(void);

/* A VarId's stable source identity and its activation epoch.  These
 * accessors are inline because matching and substitution consult the
 * coordinate at every variable edge; atom.c emits external definitions as
 * well for binary callers. */
inline uint64_t var_base_id(VarId id) {
    return id.base;
}

inline CettaFrameIdentity var_epoch_suffix(VarId id) {
    return id.epoch;
}

inline VarId var_epoch_id(VarId id, CettaFrameIdentity epoch) {
    uint64_t base = id.base;
    if (base == 0u)
        base = fresh_var_id().base;
    return (VarId){base, epoch};
}

#ifdef CETTA_TEST_HOOKS
/* Single-threaded boundary hook; production builds cannot reset identity. */
void fresh_var_id_test_reset(uint64_t next_base);
#endif

extern VarInternTable *g_var_intern;

/* ── Constructors ───────────────────────────────────────────────────────── */

Atom *atom_symbol(Arena *a, const char *name);
Atom *atom_symbol_data(Arena *a, const char *name);
Atom *atom_symbol_data_n(Arena *a, const char *bytes, uint32_t len);
/* A symbol spelled by static, immutable storage. Uses the literal cache;
 * dynamic or mutable name buffers must use atom_symbol instead. */
Atom *atom_symbol_literal(Arena *a, const char *name);
Atom *atom_symbol_id(Arena *a, SymbolId sym_id);
Atom *atom_symbol_copy(Arena *owner, const Atom *source);
bool atom_symbol_is_spelled(const Atom *atom);
/* Borrowed bytes: a spelled value's pointer is valid only while its owning
 * arena remains alive and unreset. Copy the value into a surviving owner
 * before a boundary that can reset/evacuate it; registration alone does not
 * extend this pointer's lifetime. Numbered table payloads remain permanent
 * in this prototype. Embedded NULs require atom_symbol_length, not strlen. */
const char *atom_symbol_bytes(const Atom *atom);
uint32_t atom_symbol_length(const Atom *atom);
uint64_t atom_symbol_spelling_hash(const Atom *atom);
/* Current lookup does not mutate an atom or insert a missing spelling. */
SymbolId atom_symbol_lookup_spelling_id(const Atom *atom);
static inline SymbolId atom_symbol_current_id(const Atom *atom) {
    if (!atom) return SYMBOL_ID_NONE;
#if CETTA_MANAGED_SYMBOLS
    if (atom->kind == ATOM_SYMBOL && atom->symbol_number == SYMBOL_ID_NONE &&
        atom->spelling)
        return atom_symbol_lookup_spelling_id(atom);
#endif
    return atom->symbol_number;
}
/* Registration is explicit; retaining/copying an Atom must not call it. */
SymbolId atom_symbol_register_id(const Atom *atom);
bool atom_symbols_equal(const Atom *left, const Atom *right);
bool atom_symbol_equals_cstr(const Atom *atom, const char *text);
typedef struct {
    uint64_t retained_count, retained_bytes, allocated_bytes;
} AtomManagedSymbolStats;
void atom_managed_symbol_stats(AtomManagedSymbolStats *out);
/* Visits symbol IDs and variable spellings/name keys in a syntax graph.
 * Grounded resource interiors are deliberately outside this visitor: it is
 * not a complete runtime-root scanner. Shared nodes are visited once. */
typedef bool (*AtomSymbolVisitor)(SymbolId id, void *context);
bool atom_visit_syntax_symbols(const Atom *root, AtomSymbolVisitor visit,
                               void *context);
void atom_pin_syntax_symbols(const Atom *root);
/* The symbol's one atom owned by the symbol table (no arena): for atoms a
 * compiled program hands to terms that may outlive it.  NULL without a
 * table or for an id it does not hold. */
Atom *atom_symbol_canonical(SymbolId sym_id);
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
/* A fresh private slot with source's presentation. The caller's scope keeps
   the frame alive until the returned variable is owned by its arena. */
Atom *atom_fresh_private_var(Arena *a, CettaFrameIdentityScope *scope,
                             Atom *source);
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
#ifdef CETTA_TEST_HOOKS
size_t atom_string_storage_references_test(const Atom *atom);
#endif
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
/* Immutable native value. The atom retains a hold until its arena retires. */
Atom *atom_tensor(Arena *a, CettaDenseTensorV1 *value);

/* A seal object (seal.h) begins with a pointer to its hold: a static
 * table of the object's class, so the hold is shared by all objects.
 * An arena holding an atom of the object retains it once (per lifetime
 * segment) and releases it on the reset or free that drops the atom.
 * `display` writes the public observer only, `(Sealed NICK)` and the like:
 * never an address, counter or payload.  `identity` is a private stable
 * mixing key for hashing, consistent with identity equality; it is never
 * printed, compared across processes or accepted from a program. */
typedef struct CettaSealHold {
    void (*retain)(void *object);
    void (*release)(void *object);
    /* snprintf semantics: writes at most `capacity` bytes, NUL included,
     * and returns the length of the whole observer text. */
    size_t (*display)(const void *object, char *buffer, size_t capacity);
    uint64_t (*identity)(const void *object);
    /* The object's type symbol for get-type: Sealed, SealFacet or Key. */
    const char *(*type_name)(const void *object);
} CettaSealHold;

/* The atom of seal object `object`, retained by `a`.  NULL when the object
 * is not a seal object or the arena cannot record its hold. */
Atom *atom_seal(Arena *a, void *object);
/* The seal object of a GV_SEAL atom, borrowed for as long as an arena holds
 * the atom, or NULL. */
void *atom_seal_object(const Atom *atom);
static inline const CettaSealHold *cetta_seal_hold_of(const void *object) {
    return object ? *(const CettaSealHold *const *)object : NULL;
}
static inline bool atom_is_seal(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_SEAL;
}
/* Whether `atom` is or contains an authority object (a seal facet, capsule
 * or key) anywhere in its expression structure.  Portable encoders refuse
 * such values instead of dropping or printing their authority. */
bool atom_contains_seal(const Atom *atom);
/* The observer text of a GV_SEAL atom into `buffer` (snprintf semantics);
 * 0 for any other atom. */
size_t atom_seal_display_text(const Atom *atom, char *buffer, size_t capacity);
/* The private identity key of a GV_SEAL atom (0 for any other atom), for
 * hashing and ordering consistent with identity equality.  Never printed. */
uint64_t atom_seal_identity(const Atom *atom);
/* The type symbol name of a GV_SEAL atom, or NULL. */
const char *atom_seal_type_name(const Atom *atom);

/* A structural value begins with a pointer to its process-lifetime class
 * descriptor. It owns its complete immutable data, contains no variables or
 * capabilities, and has thread-safe retain/release. All layouts of one
 * semantic type use the same descriptor. compare is a total order with
 * compare(a,b)==0 iff equal(a,b); equal values have identical hash values.
 * Display is a bounded description, not a serialization or authority token.
 * Explicit inspection may materialize ordinary data. Storage codecs are
 * separate: no portable encoding is inferred from this callback table. */
typedef struct CettaStructuralClass {
    const char *type_name;
    void (*retain)(void *value);
    void (*release)(void *value);
    bool (*equal)(const void *left, const void *right);
    int (*compare)(const void *left, const void *right);
    uint64_t (*hash)(const void *value);
    size_t (*display)(const void *value, char *buffer, size_t capacity);
    Atom *(*inspect)(const void *value, Arena *arena);
    size_t (*retained_bytes)(const void *value);
} CettaStructuralClass;

static inline const CettaStructuralClass *cetta_structural_class(const void *value) {
    return value ? *(const CettaStructuralClass *const *)value : NULL;
}
static inline bool atom_is_structural(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRUCTURAL;
}
Atom *atom_structural(Arena *arena, void *value);
bool atom_structural_equal(const Atom *left, const Atom *right);
int atom_structural_compare(const Atom *left, const Atom *right);
uint64_t atom_structural_hash(const Atom *atom);
size_t atom_structural_display(const Atom *atom, char *buffer, size_t capacity);
const char *atom_structural_type_name(const Atom *atom);
Atom *atom_structural_inspect(const Atom *atom, Arena *arena);
size_t atom_structural_retained_bytes(const Atom *atom);

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
/* Whether a held value may occur anywhere in `atom`. A negative answer is
 * complete; refused metadata traversal conservatively gives true. */
bool atom_contains_prime_held(const Atom *atom);
/* The syntax a value holds, at every depth: held wrappers removed.  For a
   position that takes syntax as such, a held value is that syntax. */
Atom *atom_prime_held_strip(Arena *a, Atom *atom);
/* Children-first image of the expressions selected by descend. Each distinct
 * selected node is rebuilt once; shared edges retain one image. Unselected
 * nodes are borrowed unchanged, so their owners must remain live. This is
 * a projection, not independent ownership transport. A selected cycle or
 * allocation/refusal returns NULL without changing source nodes. */
Atom *atom_graph_image(void *context, Arena *arena, Atom *root,
    bool (*descend)(void *, Arena *, Atom *),
    Atom *(*build)(void *, Arena *, Atom *, Atom **, bool));
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
/* The braces node {elems...}. */
Atom *atom_prime_braces(Arena *a, Atom *const *elems, CettaExprLen len);
/* The rest of a sequence after its first element, of the sequence's kind:
 * `elems` and `len` are its atom_sequence_view. */
Atom *atom_sequence_rest(Arena *a, Atom *sequence, Atom *const *elems,
                         CettaExprLen len);
/* Prime's `==`: equality of the authored terms, an elaborated template's own
 * list (hidden metadata) left out. */
bool atom_prime_authored_eq(Atom *a, Atom *b);
/* [elems... | rest]: a list when rest is a list value (their concatenation),
 * otherwise the list pattern (LIST_REST elems... rest). */
Atom *atom_list_with_rest(Arena *a, Atom *const *elems, CettaExprLen len, Atom *rest);
/* The list of the elements of list from index from on. */
Atom *atom_list_tail(Arena *a, const Atom *list, CettaExprLen from);
/* A sequence of elems of the same kind as like: a list when like is one,
 * otherwise an expression. */
Atom *atom_sequence_like(Arena *a, const Atom *like, Atom *const *elems,
                         CettaExprLen len);
/* Iteration view of a held sequence: move the container's data role to its
 * elements, retaining sequence kind and borrowed payload nodes. Unheld inputs
 * pass through. A held non-sequence or failed construction returns NULL. */
Atom *atom_sequence_data_view(Arena *arena, Atom *sequence);
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
/* A read-only expression header over borrowed coordinates. The header and
 * vector are valid only for the consuming call; neither may be retained or
 * returned as an owned result. Structural summaries grant no ownership. */
typedef struct { Atom root; } AtomExprBorrowedView;
bool atom_expr_borrowed_view_init(AtomExprBorrowedView *out,
                                  Atom **elems, CettaExprLen len);

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
/* Unpublished expression assembly in an arena with interning disabled.
 * Each append writes and summarizes one child; finish does no child traversal.
 * The caller holds the arena, child graphs and intern environment unchanged
 * until free. Missing/excess children, reset/replaced storage, and list-rest
 * splicing refuse publication. Use the ordinary constructor for list splicing
 * or interning. Allocation is sized by len; these are not elapsed-work bounds.
 * Free cancels unpublished assembly or releases its fixed control record;
 * completed atom storage remains in the arena. No draft pointer escapes. */
typedef struct AtomExprAssembly AtomExprAssembly;
AtomExprAssembly *atom_expr_assembly_new(Arena *a, CettaExprLen len);
bool atom_expr_assembly_append(AtomExprAssembly *assembly, Atom *child);
Atom *atom_expr_assembly_finish(AtomExprAssembly *assembly);
/* Allow the ordinary list-rest finishing provider after the same retained
 * child-summary assembly. Its splice/allocation has a separate atomic cost;
 * the raw-expression finish above continues to refuse list-rest splicing. */
Atom *atom_expr_assembly_finish_splicing(AtomExprAssembly *assembly);
void atom_expr_assembly_free(AtomExprAssembly *assembly);
/* Compatibility wrapper; immutable atoms are shared universally when the
   global hash-cons table is active. */
Atom *atom_expr_shared(Arena *a, Atom **elems, CettaExprLen len);
/* `expression` marked as a value HE's evaluation answered: itself when it
 * carries the mark or is not an expression, otherwise a copy that does. */
Atom *atom_he_evaluated_copy(Arena *a, Atom *expression);
/* An expression of `elems` carrying the evaluated mark, never interned. */
Atom *atom_he_evaluated_expr(Arena *a, Atom **elems, CettaExprLen len);
Atom *atom_expr_rebuild(Arena *a,const Atom *source,Atom **children,CettaExprLen len);
/* Deep copies made between enter and leave drop HE's evaluated marks: a
 * space stores syntax, never a value's evaluation state. */
void atom_deep_copy_strip_he_marks_enter(void);
void atom_deep_copy_strip_he_marks_leave(void);
Atom *atom_expr2(Arena *a, Atom *a1, Atom *a2);
Atom *atom_expr3(Arena *a, Atom *a1, Atom *a2, Atom *a3);

/* ── Special atoms ──────────────────────────────────────────────────────── */

Atom *atom_empty(Arena *a);     /* Symbol "Empty" */
Atom *atom_unit(Arena *a);      /* Expression () — empty expr */
/* The empty expression's one atom for the process, owned by no arena. */
Atom *atom_unit_canonical(void);
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
/* Metatype identity in the current symbol context, without constructing an atom. */
SymbolId atom_meta_type_id(const Atom *atom);
Atom *get_meta_type(Arena *a, Atom *atom);  /* Meta-type of atom */
bool atom_is_meta_type(Atom *type);
bool atom_meta_type_accepts(Arena *a, Atom *formal, Atom *actual);

/* ── Predicates ─────────────────────────────────────────────────────────── */

/* Compare name contents; unlike literal constructors, mutable buffers and
 * names belonging to a separately owned prepared artifact are valid here. */
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

/* A set of atoms by address.  A value shares subterms, and a walk that asks
 * a question of each subterm need ask it once per distinct subterm. */
typedef struct {
    const Atom **slots;
    size_t cap;
    size_t len;
} AtomVisitSet;

/* Whether `atom` was not yet in the set; it is in it afterwards.  False
 * also where the set cannot grow, with *fault set: the caller then walks on
 * without it. */
bool atom_visit_set_add(AtomVisitSet *set, const Atom *atom, bool *fault);
void atom_visit_set_free(AtomVisitSet *set);

/* atom_tree_any for a predicate whose answer at a node does not depend on
 * the path to it: each distinct subexpression is visited once. */
bool atom_graph_any(const Atom *root, AtomTreePredicate predicate,
                    void *context);
/* A barrier is a semantic value boundary: neither the predicate nor its
 * children are observed there. Graph predicates and barriers are independent
 * of the path by which a subterm is reached. */
bool atom_tree_any_pruned(const Atom *root, AtomTreePredicate predicate,
                          AtomTreePredicate barrier, void *context);
bool atom_graph_any_pruned(const Atom *root, AtomTreePredicate predicate,
                           AtomTreePredicate barrier, void *context);
/* Read-only admission with a bounded number of predicate observations.
 * Barriers and already visited subexpressions need no observation. Budget
 * or allocation refusal sets *complete false; false then certifies nothing.
 * COMPLETE is required. No terms or source effects are changed. */
bool atom_graph_any_checked(const Atom *root, AtomTreePredicate predicate,
                            AtomTreePredicate barrier, void *context,
                            size_t allowance, bool *complete);

/* ── Comparison ─────────────────────────────────────────────────────────── */

bool atom_eq(Atom *a, Atom *b);

/* Structural comparison in the evaluator's currently held Space read view.
 * Only Space leaves gain interpretation; State keeps its existing comparison
 * relation. The view and all resource owners must
 * remain held for the call. Representation hashes, interning and captured
 * identity continue to use atom_eq/atom_capture_identical. Without an
 * evaluator provider this is ordinary structural comparison. */
bool atom_eq_with_resolved_spaces(Atom *a, Atom *b);

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

/* Exact scalar-data key comparison on the same finite DAG domain, also
 * admitting inert internal tags. Numeric kinds and float bits are distinct;
 * variables retain their identity, spelling and structural name. No runtime
 * handle, value comparison callback or persistent cache participates. */
bool atom_data_identical(Atom *left, Atom *right);
/* Agreement of captured inputs, independent of language-level equality.
 * Immutable data and Prime contexts are compared structurally; value/code
 * marks, variable identities and names are retained. State/Space and opaque
 * service leaves retain object identity; Need capabilities retain their full
 * session/cell/authority/rights tuple. No foreign equality callback runs.
 * This borrows the complete graphs and resource owners; agreement grants no
 * lifetime, current-resource observation or invocation authority. */
bool atom_capture_identical(Atom *left, Atom *right);
CettaMatchCompletion atom_capture_identical_checked(Atom *left, Atom *right);
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
/* The module name a grounded space prints by (an HE module space), or NULL.
   It is found by the space's identity and never reads the space itself,
   which may have ended.  Weak: the evaluator supplies it, and a binary
   without one prints every space by address. */
const char *space_module_display_name(const void *space)
    __attribute__((weak));
char *atom_to_parseable_string(Arena *a, Atom *atom);
char *atom_to_parseable_string_petta(Arena *a, Atom *atom);
/* The printed bytes exactly, with their length: where a lane writes a
 * string's bytes raw, NUL included. */
char *atom_to_parseable_bytes(Arena *a, Atom *atom, bool petta,
                              size_t *len_out);
/* Source output uses the same dialect printer but preserves source variable
 * names rather than replacing them with display ordinals. This is not a
 * guarantee of reader round-trip; the source adapter checks that separately. */
char *atom_to_source_bytes(Arena *a, Atom *atom, bool petta,
                          size_t *len_out);

/* Deep-copy an atom DAG into a different arena, preserving source pointer
   sharing within one copy episode. */
Atom *atom_deep_copy(Arena *dst, Atom *src);
/* A single publication root, with the same ownership policy as a default
 * copy session: reuse a destination-closed graph, otherwise copy it while
 * preserving its internal sharing. Related exported roots still require a
 * shared session. */
Atom *atom_deep_copy_single(Arena *dst, Atom *src);
typedef Atom *(*AtomDeepCopyResolver)(void *context, Atom *src);
typedef Atom *(*AtomDeepCopyLeafMap)(Arena *dst, Atom *src, void *context);
/* A fixed observation policy may expose another expression's children, or
 * finish an opaque value without traversing it. The original source pointer
 * remains the memo key. A completed value must be owned by dst; returned views
 * and their children remain live throughout this synchronous episode. The
 * observed graph must be finite and acyclic. Views may expand a list carrier,
 * whose produced edges remain part of the operation's work/output account.
 * On failure, discard callback-owned inventories before rolling back dst. */
typedef bool (*AtomDeepCopyNodeView)(Arena *dst, Atom *src, Atom **view,
                                   bool *complete, void *context);
Atom *atom_deep_copy_observed(Arena *dst, Atom *src,
                             AtomDeepCopyNodeView view, void *context);
/* A leaf map must have one stable interpretation for the whole episode.
 * It may allocate in dst and return NULL on failure. Any callback-owned
 * inventory must be discarded before the caller rolls back dst. */
Atom *atom_deep_copy_mapped(Arena *dst, Atom *src,
                           AtomDeepCopyLeafMap map, void *context);
/* A multi-root copy episode.  Every call shares one source-pointer forwarding
   table, so pointer-DAG sharing is preserved across separately named roots.
   A destination-owned root is reused only when its compositional arena-closure
   flag proves that the full Atom-child graph is already safe.  An optional
   resolver, installed before copying, redirects every encountered node before
   traversal; update-cell collectors use it to collapse evaluated thunks. */
AtomDeepCopySession *atom_deep_copy_session_new(Arena *dst);

/* Copy an admitted immutable value graph into fresh owned storage, including
 * global hashcons nodes. Every source Atom gets a distinct image; repeated
 * Atom edges and variable name keys use the same forwarding memo. Sources and
 * their payload owners remain live and quiescent until the session ends.
 * Symbols/variables/expressions and ordinary scalar values are admitted.
 * Grounded resources, native-handle integers and opaque graphs refuse; this
 * profile does not acquire their lifetimes. Refusal may leave unpublished
 * destination preparation, which the caller still owns. Child-array address
 * equality is not an observation of this value-graph snapshot.
 * The destination must have neither hashcons nor scalar canonicalization.
 * This is separate from allocation-domain relocation and destructive GC. */
bool atom_deep_copy_session_set_snapshot(AtomDeepCopySession *session);
bool atom_deep_copy_session_snapshot_unsupported(const AtomDeepCopySession *session);
/* Inspect an already completed forwarding entry in an actual, current
 * snapshot session. This neither copies a root nor extends either lifetime.
 * The destination header and all source memo keys remain independently held. */
Atom *atom_deep_copy_session_snapshot_image(const AtomDeepCopySession *session,
    const Arena *destination, const Atom *source);

typedef struct {
    const Arena *source;
    Arena *destination;
} AtomDeepCopyArenaPair;
/* Optional allocation-domain transport, installed before copying any root.
 * Every non-global Atom owner must have one injective pair. An identity pair
 * explicitly borrows a separately retained external region: only its closed
 * Atom graphs are reused, and that region must outlive the destination. This
 * clause does not acquire a pin or validate a Space's semantic revision.
 * Borrowed regions are excluded from source-retirement claims. Older-generation
 * links, allocation policy and live scalar-cache entries are transported;
 * hash-consed owners and reset/configuration drift refuse. The caller owns
 * all destination arenas and keeps source arenas quiescent until the session
 * is freed. This neither retains source owners nor copies grounded resource
 * identities. Global closed atoms keep their existing lifetime contract.
 * A failed setup/copy leaves sources unchanged; discard the unpublished
 * session and destination arenas, which may contain partial preparation. */
bool atom_deep_copy_session_set_arena_map(AtomDeepCopySession *session,
    const AtomDeepCopyArenaPair *pairs, size_t count);

/* GlobalDomainInspectionV2: index visits include table/closure/subset slots,
 * memo initialization and growth clears, and every lookup/store/rehash probe.
 * Analysis steps include node headers and allocation-attempt sites. The sum
 * is bounded by the supplied service allowance on success and refusal.
 * It excludes allocator internals, elapsed CPU and the fixed authority guards. */
typedef struct {
    size_t index_visits, analysis_steps;
} AtomDeepCopyGlobalInspection;

/* Before arena-map setup, restrict a fresh copy session to one held current
 * runtime hashcons table, built-in static list nodes, and the exact published
 * canonical-symbol/unit prefix of the same symbol-table instance. Admission
 * checks the complete table's closure by pointer membership, without
 * dereferencing foreign children. Source/table/symbol owners must remain
 * live and quiescent. Reclamation, runtime-table or symbol-instance changes
 * invalidate the session. This opt-in acquires no lifetime pin and leaves
 * ordinary mapped copying unchanged. No copying is allowed until map setup,
 * including its canonical-cache transport. Inspection has an explicit finite bound;
 * the required output retains the complete named service-loop count, including
 * refused setup. The caller accounts for that work in its own scope. */
bool atom_deep_copy_session_restrict_globals(AtomDeepCopySession *session,
    const HashConsTable *owner, size_t inspection_limit,
    AtomDeepCopyGlobalInspection *inspection);

/* Extend that same held serial domain after actual append-only allocation.
 * Both calls reject owner, reclamation, symbol or mapped-arena drift, even
 * when the used count matches. Existing logical cells must be immutable
 * under the native hashcons service contract. A changed count causes a full
 * current-table closure check and an old-key subset check. No child header
 * is read before owner membership. This acquires no owner lifetime.
 * The budget is additional temporary metadata beyond the still-retained old
 * carrier, and bounded inspection work. Reserve both before allocating.
 * A refused extension leaves the old domain/count and every forwarding map
 * unchanged; partial inspection is returned for the caller's paid account.
 * A successful extension replaces only the membership carrier. */
bool atom_deep_copy_session_global_extension_budget(const AtomDeepCopySession *,
    const HashConsTable *, size_t node_limit, size_t *additional_bytes, size_t *work);
bool atom_deep_copy_session_extend_globals(AtomDeepCopySession *,
    const HashConsTable *, size_t node_limit, size_t inspection_limit,
    size_t additional_bytes, AtomDeepCopyGlobalInspection *);

/* Explicit serial runtime profile, after successful restrict_globals.
 * Each pair keeps its allocation policy: both hashcons pointers are NULL,
 * or both identify the exact validated and independently held runtime table.
 * Private Atom/header and element-buffer aliases use the same injective copy
 * session; no canonical constructor may merge the copied private variables.
 * This does not isolate mutable service state or acquire the table lifetime.
 * Ordinary set_arena_map continues to reject every hashcons-backed pair. */
bool atom_deep_copy_session_set_runtime_arena_map(AtomDeepCopySession *session,
    const AtomDeepCopyArenaPair *pairs, size_t count);

bool atom_deep_copy_session_maps_arena(const AtomDeepCopySession *session,
    const Arena *source, const Arena *destination);
/* Validate the allocation destination before composing carrier promotion
 * with an existing multi-root copy. This does not establish source lifetime
 * or ownership of resources referenced by grounded values. */
bool atom_deep_copy_session_targets(const AtomDeepCopySession *session,
                                    const Arena *dst);
/* A nested carrier allocates its metadata and frame-identity pins in its
 * own mapped private destination, while all Atom roots keep the enclosing
 * session's forwarding table. Scopes are zero-initialized, entered only on
 * a quiescent mapped session, and left in reverse order before session/arena
 * release. One mutable session must not be used concurrently. Identity-pair
 * external regions, unmapped owners and stale maps refuse without changes.
 * Leave restores the previous selection even after a failed copy or epoch
 * invalidation; an out-of-order leave refuses. Inactive leave is idempotent. */
typedef struct AtomDeepCopyDestinationScope {
    AtomDeepCopySession *session;
    Arena *previous;
    Arena *selected;
    struct AtomDeepCopyDestinationScope *parent;
    bool active;
} AtomDeepCopyDestinationScope;
bool atom_deep_copy_destination_enter(AtomDeepCopySession *session,
    Arena *destination, AtomDeepCopyDestinationScope *scope);
bool atom_deep_copy_destination_leave(AtomDeepCopyDestinationScope *scope);
/* Typed, injective metadata identities belonging to the same unpublished
 * graph as the Atom memo. Kinds are stable caller-owned tokens, not names or
 * value equality. Ordinary registration borrows both endpoints; payload
 * storage belongs to the destination owner. A partially filled graph is
 * discarded on any copy refusal. Ordinary Atom copying does not construct
 * these tables. */
/* Check the captured map and any registrations involving this endpoint
 * before inspecting its payload. A missing registration is allowed and
 * still requires the caller to supply a live object. This does not permit
 * reusing unrelated Atom memo entries after an arena reset. */
bool atom_deep_copy_session_object_current(const AtomDeepCopySession *session,
    const void *kind, const void *object);
void *atom_deep_copy_session_object_forwarded(const AtomDeepCopySession *session,
    const void *kind, const void *source, size_t bytes);
/* Inverse lookup in that same injective table, with the same captured-map
 * validation. This identifies an already registered source; it does not
 * establish the lifetime or authority of either endpoint. */
const void *atom_deep_copy_session_object_original(const AtomDeepCopySession *session,
    const void *kind, const void *image, size_t bytes);
bool atom_deep_copy_session_object_record(AtomDeepCopySession *session,
    const void *kind, const void *source, void *image, size_t bytes);
/* Retain both metadata endpoints until session release. Callbacks must be
 * non-failing, must not reenter the session, and must not claim ownership of
 * the endpoints' referenced arenas. An exact duplicate adds no holds; a
 * conflicting registration leaves all holds unchanged. The selected
 * destination's identity and reset epoch are captured for later lookup.
 * These holds retain metadata, not copied payload bytes. */
typedef void (*AtomDeepCopyObjectLifetime)(const void *object);
bool atom_deep_copy_session_object_record_retained(AtomDeepCopySession *session,
    const void *kind, const void *source, void *image, size_t bytes,
    AtomDeepCopyObjectLifetime retain, AtomDeepCopyObjectLifetime release);
/* Resolve actual owner/extent under the captured region map. Without a map,
 * the supplied source owner is checked and the selected destination is used.
 * Identity pairs may be returned, but confer no writable or lifetime licence.
 * A metadata client must validate all edges before retaining such an image. */
bool atom_deep_copy_session_destination_for_owner(
    const AtomDeepCopySession *session, const Arena *source, Arena **image);
bool atom_deep_copy_session_owner_of_span(const AtomDeepCopySession *session,
    const void *source, size_t bytes, const Arena *fallback,
    const Arena **source_owner, Arena **image_owner);
/* Copy a quiescent, table-backed transport without copying its atoms. Both
 * sessions borrow the same source graphs and destination arena; those owners
 * must outlive both sessions. Existing forwarding and buffer-sharing entries
 * are preserved, while later table updates are independent. A resolver or
 * destructive region transport has additional authority and is refused.
 * Metadata registrations, borrowed or retained, also refuse cloning.
 * Refusal leaves the source unchanged. This is not an arena snapshot. */
AtomDeepCopySession *atom_deep_copy_session_clone(
    const AtomDeepCopySession *source);
/* A plain, non-interning enclosing copy for retained pointer-key state. */
bool atom_deep_copy_session_relocation_targets(
    const AtomDeepCopySession *session, const Arena *destination);
/* Relocate a quiescent transport's keys, values and shared-buffer keys through
 * one enclosing copy session. Both sessions must be plain table transports;
 * the destination must not intern distinct source nodes. Each known shared
 * source end must map to one distinct destination end, or the copy refuses.
 * The source is unchanged. On refusal the caller discards the unpublished
 * enclosing copy and its arena; they may contain partially copied roots.
 * Atom inputs retain copy()'s finite expression-DAG contract. Grounded
 * resource identities keep their separate lifetime contract. */
AtomDeepCopySession *atom_deep_copy_session_relocate(
    const AtomDeepCopySession *source, AtomDeepCopySession *relocation);
/* Borrow closed graphs from the named arenas instead of transporting them.
 * Install before the first copy; mapped/resolving sessions cannot borrow.
 * The identity array stays live until this session ends. Source storage stays
 * live until every resulting borrow is retired or materialized by an ordinary
 * copy. This grants no destination-ownership or arena-closure certificate. */
bool atom_deep_copy_session_loan_arenas(AtomDeepCopySession *session,
    const uint32_t *identities, size_t count);
/* A multi-root mapped episode. The mapper/context cannot be replaced during
 * the episode. Destination-owned terms are still traversed: storage ownership
 * does not establish that their leaves already have the requested meaning.
 * The caller keeps sources/context live, stages returned roots, and publishes
 * them only after all copies succeed. After failure, discard the session and
 * its callback inventory before reclaiming abandoned destination storage. */
AtomDeepCopySession *atom_deep_copy_session_new_mapped(
    Arena *dst, AtomDeepCopyLeafMap map, void *context);
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
typedef enum {
    ATOM_DEEP_COPY_PAUSED,
    ATOM_DEEP_COPY_COMPLETE,
    ATOM_DEEP_COPY_REFUSED,
} AtomDeepCopyStep;
/* Retain one root's traversal in the same copy episode. This interface admits
 * ordinary, nonmapped finite expression DAGs; the specialized snapshot,
 * resolver, mapped-arena and dying-region profiles keep their existing APIs.
 * A grant counts the initial root decision, child decisions and expression
 * finalizations. It does not bound scalar copying, memo operations, allocation,
 * constructor work or cleanup, and is not an instruction or time budget.
 * Zero grant is inert, including before the first copy. On PAUSED, out is NULL
 * and the exact stack and forwarding memo remain owned by the session. The
 * caller holds all sources, destination and intern context stable. Only the
 * same root may resume; clone, relocation and changing the profile refuse.
 * COMPLETE publishes the whole root and keeps the episode's shared memo for
 * subsequent roots. Freeing the session cancels an unpublished traversal;
 * destination storage remains owned by its arena. No partial image is exposed.
 * REFUSED never acknowledges a source return. A failed traversal must be
 * discarded; rejection of another root/profile leaves an active one intact. */
AtomDeepCopyStep atom_deep_copy_session_advance(AtomDeepCopySession *session,
    Atom *src, uint64_t allowance, Atom **out, uint64_t *used);
bool atom_deep_copy_session_pending(const AtomDeepCopySession *session);
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
bool atom_deep_copy_session_failed(const AtomDeepCopySession *session);
/* Validate the selected metadata destination without exposing its address. */
bool atom_deep_copy_session_private_destination(const AtomDeepCopySession *session);
void atom_deep_copy_session_free(AtomDeepCopySession *session);
void atom_deep_copy_session_storage_census(const AtomDeepCopySession *session,
    CettaStorageCensus *census);
/* Companion graph owners share one relocation inventory per copy episode.
 * Keys identify a caller's inventory kind, not content. Installed inventories
 * own their entries and release them when the session ends, including failure.
 * No inventory or source-pointer key survives into another episode. A
 * supplied storage visitor describes its actual retained edges; without one,
 * a nonempty inventory remains opaque to bounded census. Inventories do not
 * acquire a clone/relocation operation from having a storage visitor. */
typedef void (*AtomDeepCopyInventoryRelease)(void *inventory);
void *atom_deep_copy_session_inventory(const AtomDeepCopySession *session,
                                      const void *kind);
bool atom_deep_copy_session_install_inventory(AtomDeepCopySession *session,
    const void *kind, void *inventory, AtomDeepCopyInventoryRelease release,
    CettaStorageVisitor storage);
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


#include "checkpoint_compare.h"

static inline bool cetta_checkpoint_frame(const CettaCheckpointCompare *read,
    const char *site, CettaFrameIdentity left, CettaFrameIdentity right) {
    return read->identity(read->context, site, CETTA_CHECKPOINT_FRAME,
                          left.word, right.word);
}

static inline bool cetta_checkpoint_var(const CettaCheckpointCompare *read,
    const char *site, VarId left, VarId right) {
    if (var_id_is_none(left) || var_id_is_none(right))
        return read->identity(read->context, site, CETTA_CHECKPOINT_VAR,
                              var_id_is_none(left) ? 0u : 1u,
                              var_id_is_none(right) ? 0u : 1u);
    if (left.base == 0u || right.base == 0u)
        return false;
    return read->identity(read->context, site, CETTA_CHECKPOINT_VAR,
                          left.base, right.base) &&
           read->identity(read->context, site, CETTA_CHECKPOINT_FRAME,
                          left.epoch.word, right.epoch.word);
}

bool atom_checkpoint_global_budget(const HashConsTable *, size_t, size_t *, size_t *);
bool atom_checkpoint_global_contains(const AtomDeepCopySession *, const Atom *, const CettaCheckpointCompare *);
bool arena_checkpoint_span(const Arena *, const void *, size_t, const CettaCheckpointCompare *, bool);
bool arena_checkpoint_cache_compare(const Arena *, const Arena *, const CettaCheckpointCompare *);
bool arena_checkpoint_frame_holds(const Arena *, const CettaCheckpointCompare *, bool);
bool arena_checkpoint_holds_frame(const Arena *, CettaFrameIdentity, const CettaCheckpointCompare *, bool);
bool atom_checkpoint_payload_admitted(const Atom *, const Arena *, bool, const CettaCheckpointCompare *, bool);
bool atom_checkpoint_leaf_compare(const Atom *, const Atom *, const CettaCheckpointCompare *);
/* Same externally held owner, never an image of that owner's mutable resource. */
bool atom_checkpoint_native_reference_compare(const Atom *, const Arena *,
    const Atom *, const Arena *, const CettaCheckpointCompare *);
/* A rigid private tag readout, not admission to execute its enclosing form. */
bool atom_checkpoint_internal_tag_admitted(int64_t);
#endif /* CETTA_ATOM_H */
