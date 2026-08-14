#ifndef CETTA_ATOM_H
#define CETTA_ATOM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "session.h"
#include "symbol.h"

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
    GV_INTERNAL_TAG
} GroundedKind;

typedef enum {
    CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND = 1,
    CETTA_INTERNAL_TAG_PETTA_COUNTED_COLLECTION = 2,
    CETTA_INTERNAL_TAG_PRIME_LEXICAL_SLOT = 3,
} CettaInternalTag;

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
 * Constructor-built atoms establish term-retention stability under the same
 * leaf and child-fold judgment as structural-hash stability.  Keep the
 * semantic name explicit for callers; split the bits before either judgment
 * is widened.  The summary assumes expression children remain immutable.
 */
#define ATOM_FLAG_TERM_STABLE ATOM_FLAG_HASH_STABLE
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
    VarId var_id;            /* ATOM_VAR only */
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
    union {
        struct {            /* ATOM_GROUNDED */
            GroundedKind gkind;
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
    /*
     * Monotone allocation epoch.  Arena identity survives mark/reset, while
     * reset_epoch changes whenever reset or free invalidates owned pointers.
     * Derived pointer caches must key on both.
     */
    uint64_t reset_epoch;
    ArenaFinalizer *finalizers;
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
} ArenaMark;

void *cetta_malloc(size_t size);
void *cetta_realloc(void *ptr, size_t size);
void  arena_init(Arena *a);
void  arena_free(Arena *a);
void  arena_reserve(Arena *a, size_t size);
void  arena_set_hashcons(Arena *a, HashConsTable *hc);
void  arena_set_runtime_kind(Arena *a, CettaArenaRuntimeKind kind);
size_t arena_accounted_live_bytes(const Arena *a);
size_t arena_mark_accounted_live_bytes(ArenaMark mark);
void  arena_account_external_bytes(Arena *a, size_t size);
ArenaMark arena_mark(const Arena *a);
void  arena_reset(Arena *a, ArenaMark mark);
void *arena_alloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *s);
bool  arena_owns_ptr(const Arena *a, const void *ptr);
bool  arena_owns_atom(const Arena *a, const Atom *atom);
/* Exact compositional ownership predicate: every arena-owned pointer in the
 * atom graph belongs to `arena`; globally owned immutable nodes are also
 * permitted.  Identity-bearing external resources deliberately fail. */
bool  atom_graph_is_closed_for_arena(const Arena *arena,
                                     const Atom *atom);
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
extern HashConsTable *g_hashcons;

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
VarId   var_epoch_id(VarId id, uint32_t epoch);
uint32_t var_base_id(VarId id);
uint32_t var_epoch_suffix(VarId id);

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
Atom *atom_string(Arena *a, const char *val);
Atom *atom_space(Arena *a, void *space_ptr);

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
Atom *atom_foreign(Arena *a, CettaForeignValue *value);
Atom *atom_internal_tag(Arena *a, CettaInternalTag tag);
Atom *atom_petta_prolog_compound(Arena *a, Atom *body);
bool atom_petta_prolog_compound_body(Atom *atom, Atom **body);
bool atom_prolog_compound_body(Atom *atom, Atom **body);
Atom *atom_petta_counted_collection(Arena *a, int64_t count);
bool atom_petta_counted_collection_count(
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
Atom *get_meta_type(Arena *a, Atom *atom);  /* Meta-type of atom */
bool atom_is_meta_type(Atom *type);
bool atom_meta_type_accepts(Arena *a, Atom *formal, Atom *actual);

/* ── Predicates ─────────────────────────────────────────────────────────── */

bool atom_is_symbol(Atom *a, const char *name);
bool atom_is_empty(Atom *a);
bool atom_is_error(Atom *a);
bool atom_is_empty_or_error(Atom *a);
bool atom_is_var(Atom *a);
bool atom_is_expr(Atom *a);
bool atom_is_symbol_id(Atom *a, SymbolId id);
const char *atom_name_cstr(Atom *a);
SymbolId atom_head_symbol_id(Atom *a);
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

/* ── Printing ───────────────────────────────────────────────────────────── */

void atom_print(Atom *a, FILE *out);
/* PeTTa's observable writer follows its SWI oracle: in particular it uses
   shortest round-tripping floats and does not double literal backslashes in
   string payloads. */
void atom_print_petta(Atom *a, FILE *out);
/* Print into arena-allocated string */
char *atom_to_string(Arena *a, Atom *atom);
char *atom_to_parseable_string(Arena *a, Atom *atom);

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
