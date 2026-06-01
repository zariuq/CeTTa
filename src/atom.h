#ifndef CETTA_ATOM_H
#define CETTA_ATOM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "session.h"
#include "symbol.h"

#ifndef CETTA_BUILD_WITH_GMP
#define CETTA_BUILD_WITH_GMP 0
#endif

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif

typedef struct CettaForeignValue CettaForeignValue;
typedef struct CettaBigInt CettaBigInt;
typedef struct CettaRational CettaRational;
typedef uint64_t VarId;
typedef uint64_t CettaExprLen;
typedef uint64_t CettaExprIndex;
typedef struct HashConsTable HashConsTable;
typedef struct ArenaFinalizer ArenaFinalizer;

#define VAR_ID_NONE ((VarId)0)
#define CETTA_RATIONAL_DEFAULT_MAX_DIGITS 4096u

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
    GV_RATIONAL
} GroundedKind;

#define ATOM_FLAG_HAS_VARS 0x01u
#define ATOM_FLAG_HASH_VALID 0x02u
#define ATOM_FLAG_HAS_REGISTRY_REFS 0x04u

/* ── Atom ───────────────────────────────────────────────────────────────── */

typedef struct Atom Atom;
struct Atom {
    AtomKind kind;
    uint32_t flags;
    VarId var_id;            /* ATOM_VAR only */
    SymbolId sym_id;         /* ATOM_SYMBOL, or variable spelling */
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
    HashConsTable *hashcons;
    size_t live_bytes;
    size_t reserved_bytes;
    uint32_t block_count;
    CettaArenaRuntimeKind runtime_kind;
    ArenaFinalizer *finalizers;
} Arena;

typedef struct {
    ArenaBlock *head;
    size_t used;
    size_t live_bytes;
    size_t reserved_bytes;
    uint32_t block_count;
    ArenaFinalizer *finalizers;
} ArenaMark;

void *cetta_malloc(size_t size);
void *cetta_realloc(void *ptr, size_t size);
void  arena_init(Arena *a);
void  arena_free(Arena *a);
void  arena_set_hashcons(Arena *a, HashConsTable *hc);
void  arena_set_runtime_kind(Arena *a, CettaArenaRuntimeKind kind);
ArenaMark arena_mark(const Arena *a);
void  arena_reset(Arena *a, ArenaMark mark);
void *arena_alloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *s);
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
};

void hashcons_init(HashConsTable *hc);
void hashcons_free(HashConsTable *hc);
/* Return shared atom if identical one exists, otherwise insert and return */
Atom *hashcons_get(HashConsTable *hc, Atom *atom);

/* Global hash-cons table */
extern HashConsTable *g_hashcons;

/* Fast equality: if both atoms are hash-consed, pointer comparison suffices */
bool atom_eq_fast(Atom *a, Atom *b);

/* Compute structural hash of an atom */
uint32_t atom_hash(Atom *a);

/* ── Variable identity intern/freshening ──────────────────────────────── */

typedef struct {
    SymbolId *spellings;
    VarId *ids;
    uint32_t size, used;
} VarInternTable;

void    var_intern_init(VarInternTable *t);
void    var_intern_free(VarInternTable *t);
VarId   var_intern(VarInternTable *t, SymbolId spelling);
VarId   fresh_var_id(void);
VarId   var_epoch_id(VarId id, uint32_t epoch);
uint32_t var_base_id(VarId id);
uint32_t var_epoch_suffix(VarId id);

extern VarInternTable *g_var_intern;

/* ── Constructors ───────────────────────────────────────────────────────── */

Atom *atom_symbol(Arena *a, const char *name);
Atom *atom_symbol_id(Arena *a, SymbolId sym_id);
Atom *atom_var(Arena *a, const char *name);
Atom *atom_var_with_id(Arena *a, const char *name, VarId id);
Atom *atom_var_with_spelling(Arena *a, SymbolId spelling, VarId id);
Atom *atom_int(Arena *a, int64_t val);
Atom *atom_bigint(Arena *a, const char *val);
const char *atom_bigint_cstr(const Atom *atom);
#if CETTA_BUILD_WITH_GMP
bool  atom_bigint_get_mpz(const Atom *atom, mpz_t out);
Atom *atom_bigint_from_mpz(Arena *a, const mpz_t value);
#endif
Atom *atom_rational(Arena *a, const char *val);
const char *atom_rational_cstr(const Atom *atom);
bool cetta_rational_text_exceeds_digit_limit(const char *text,
                                             uint64_t max_digits,
                                             bool *valid_out);
Atom *atom_rational_limited(Arena *a, const char *val, uint64_t max_digits,
                            bool *too_large_out);
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
} StateCell;

typedef struct {
    void *space_ptr;
    CettaEvaluatorOptions options;
} CaptureClosure;

Atom *atom_state(Arena *a, StateCell *cell);
Atom *atom_capture(Arena *a, CaptureClosure *closure);
Atom *atom_foreign(Arena *a, CettaForeignValue *value);
Atom *atom_expr(Arena *a, Atom **elems, CettaExprLen len);
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

/* ── Comparison ─────────────────────────────────────────────────────────── */

bool atom_eq(Atom *a, Atom *b);

/* ── Printing ───────────────────────────────────────────────────────────── */

void atom_print(Atom *a, FILE *out);
/* Print into arena-allocated string */
char *atom_to_string(Arena *a, Atom *atom);
char *atom_to_parseable_string(Arena *a, Atom *atom);

/* Deep-copy an atom tree into a different arena */
Atom *atom_deep_copy(Arena *dst, Atom *src);
/* Deep-copy with structural sharing for immutable atoms.
   Safe only for arenas whose contents outlive the global hash-cons table. */
Atom *atom_deep_copy_shared(Arena *dst, Atom *src);

static inline bool atom_has_vars(const Atom *atom) {
    return atom && (atom->flags & ATOM_FLAG_HAS_VARS) != 0;
}

static inline bool atom_has_registry_refs(const Atom *atom) {
    return atom && (atom->flags & ATOM_FLAG_HAS_REGISTRY_REFS) != 0;
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
