/* mm0_internal.h - structures shared by the native MM0 verifier's parts.
 *
 * Pinned sources: digama0/mm0 at 0d414c0bfdaaeb7fea571895127abc1fa5a3d956,
 * mm0.md (the language and its semantics) and mm0-c/mmb.md (the MMB format),
 * with mm0-c/{verifier,parser}.c as the reference implementation.  Rule
 * comments in the .c files cite these files by section or function. */
#ifndef CETTA_MM0_INTERNAL_H
#define CETTA_MM0_INTERNAL_H

#include "mm0.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- arena: bump allocation in growing chunks ---------------------------
 * Chunk sizes are allocation granularity, not limits.  Marks nest; a reset
 * frees everything allocated after the mark, keeping one spare chunk. */
typedef struct Mm0Chunk Mm0Chunk;
typedef struct {
    Mm0Chunk *chunk, *spare;
    char *cur, *end;
    size_t held;          /* bytes in chunks currently linked */
} Mm0Arena;
typedef struct { Mm0Chunk *chunk; char *cur; } Mm0ArenaMark;

void mm0_arena_init(Mm0Arena *arena);
void mm0_arena_free(Mm0Arena *arena);
void *mm0_arena_alloc_slow(Mm0Arena *arena, size_t size);
static inline void *mm0_arena_alloc(Mm0Arena *arena, size_t size) {
    if (size <= SIZE_MAX - 7) {
        size = (size + 7) & ~(size_t)7;
        if ((size_t)(arena->end - arena->cur) >= size) {
            void *p = arena->cur;
            arena->cur += size;
            return p;
        }
    }
    return mm0_arena_alloc_slow(arena, size);
}
Mm0ArenaMark mm0_arena_mark(const Mm0Arena *arena);
void mm0_arena_reset(Mm0Arena *arena, Mm0ArenaMark mark);
char *mm0_arena_strndup(Mm0Arena *arena, const char *text, size_t len);

/* Grow *items so that it holds at least `need` elements of `size` bytes. */
bool mm0_reserve(void **items, size_t *capacity, size_t need, size_t size);
#define MM0_RESERVE(vec, cap, need) \
    ((need) <= (cap) || mm0_reserve((void **)&(vec), &(cap), (need), sizeof *(vec)))

/* ---- name map: string -> uint32_t, open addressing ---------------------- */
typedef struct {
    const char *key;      /* not owned */
    uint32_t len;
    uint32_t value;
    uint64_t hash;
} Mm0NameSlot;
typedef struct {
    Mm0NameSlot *slots;
    size_t capacity, count;
} Mm0NameMap;

void mm0_names_free(Mm0NameMap *map);
/* Returns true and sets *value when present. */
bool mm0_names_get(const Mm0NameMap *map, const char *key, size_t len, uint32_t *value);
/* Inserts a new key (the caller has checked it is absent); the key text
 * must outlive the map. */
bool mm0_names_put(Mm0NameMap *map, const char *key, size_t len, uint32_t value);
/* Removes a key that is present. */
void mm0_names_remove(Mm0NameMap *map, const char *key, size_t len);
uint64_t mm0_hash_bytes(const void *data, size_t len);

/* ---- statements ---------------------------------------------------------
 * An expression is a variable of its declaration's context or a term
 * application.  Statement trees may share subtrees (MMB unify streams
 * save and reference subterms), so algorithms over them memoize by node. */
typedef struct Mm0Expr Mm0Expr;
struct Mm0Expr {
    uint32_t head;        /* term index, or variable index in the context */
    uint32_t nargs;       /* 0 for a variable */
    uint8_t var;
    Mm0Expr *args[];
};

#define MM0_SORT_PURE 1u
#define MM0_SORT_STRICT 2u
#define MM0_SORT_PROVABLE 4u
#define MM0_SORT_FREE 8u

typedef struct {
    const char *name;     /* NULL for a declaration with no specification name */
    uint8_t mods;
} Mm0SortDecl;

/* A binder of a declaration.  Dependency sets are bit sets over the
 * declaration's bound binders in order (bit k: the k-th bound binder), as in
 * mm0-c/mmb.md "Term Table"; they use `depw` 64-bit words, so a declaration
 * of the semantic route may have any number of bound variables. */
typedef struct {
    uint32_t sort;
    uint8_t bound;
    uint32_t ordinal;     /* bound: its index among the bound binders */
    uint64_t *deps;       /* depw words: {ordinal} if bound, else its dependencies */
} Mm0Binder;

#define MM0_DECL_DEF 1u
#define MM0_DECL_LOCAL 2u
#define MM0_DECL_AXIOM 4u

typedef struct {
    const char *name;
    uint32_t nargs, nbound, depw;
    Mm0Binder *args;
    uint32_t ret_sort;
    uint64_t *ret_deps;   /* depw words over the bound binders */
    uint8_t flags;        /* MM0_DECL_DEF, MM0_DECL_LOCAL */
    uint32_t ndummies;    /* defs: dummy variables nargs .. nargs+ndummies-1 */
    uint32_t *dummy_sorts;
    Mm0Expr *body;        /* defs */
    const char **var_names; /* nargs + ndummies entries, each may be NULL */
    uint64_t order;       /* position in the theory's declaration sequence */
} Mm0TermDecl;

typedef struct {
    const char *name;
    uint32_t nargs, nbound, depw;
    Mm0Binder *args;
    uint32_t nhyps;
    Mm0Expr **hyps;
    Mm0Expr *concl;
    uint8_t flags;        /* MM0_DECL_AXIOM, MM0_DECL_LOCAL */
    const char **var_names; /* nargs entries */
    uint64_t order;
} Mm0ThmDecl;

typedef enum { MM0_KIND_SORT, MM0_KIND_TERM, MM0_KIND_THM } Mm0DeclKind;

typedef struct { uint8_t kind; uint32_t index; } Mm0DeclRef;

struct Mm0Theory {
    Mm0Arena arena;       /* every declaration's data */
    Mm0SortDecl *sorts;
    size_t nsorts, cap_sorts;
    Mm0TermDecl *terms;
    size_t nterms, cap_terms;
    Mm0ThmDecl *thms;
    size_t nthms, cap_thms;
    Mm0DeclRef *order;    /* the declaration sequence */
    size_t norder, cap_order;
    Mm0NameMap sort_names, term_names, thm_names;
    uint64_t identity;    /* process-unique, for refusing foreign values */
};

/* Theory mutation, used by the checked routes only.  Each appends a
 * declaration whose data already lives in the theory arena. */
bool mm0_theory_push_sort(Mm0Theory *theory, const char *name, uint8_t mods);
bool mm0_theory_push_term(Mm0Theory *theory, const Mm0TermDecl *decl);
bool mm0_theory_push_thm(Mm0Theory *theory, const Mm0ThmDecl *decl);
/* Drop every declaration after the first `count`, and their names. */
void mm0_theory_rollback(Mm0Theory *theory, size_t count, Mm0ArenaMark mark);

Mm0Expr *mm0_expr_var(Mm0Arena *arena, uint32_t index);
Mm0Expr *mm0_expr_app(Mm0Arena *arena, uint32_t term, uint32_t nargs);

/* ---- step accounting -------------------------------------------------
 * Steps are counted down in windows of at most 4096; the end of a window
 * charges it against the budget and polls for interruption.  A window is
 * one step longer than the budget left, so the first step beyond the budget
 * ends a window and is refused. */
typedef struct {
    uint64_t budget, used;
    uint32_t window, until_check;
    Mm0PollFn poll;
    void *poll_context;
} Mm0Steps;

void mm0_steps_init(Mm0Steps *steps, const Mm0Options *options);
/* Returns MM0_OK, MM0_LIMIT or MM0_INTERRUPTED. */
Mm0Status mm0_steps_slow(Mm0Steps *steps);
static inline Mm0Status mm0_step(Mm0Steps *steps) {
    if (__builtin_expect(--steps->until_check != 0, 1)) return MM0_OK;
    return mm0_steps_slow(steps);
}

void mm0_error_set(Mm0Error *error, Mm0Status status, const char *reason);

/* ---- MMB command encoding (mm0-c/mmb.md "Encoding and types") ---------- */
enum {
    MMB_CMD_END = 0x00,
    /* statements */
    MMB_STMT_AXIOM = 0x02, MMB_STMT_SORT = 0x04, MMB_STMT_DEF = 0x05,
    MMB_STMT_THM = 0x06, MMB_STMT_LOCAL_DEF = 0x0D, MMB_STMT_LOCAL_THM = 0x0E,
    /* proof stream */
    MMB_PROOF_TERM = 0x10, MMB_PROOF_TERM_SAVE = 0x11, MMB_PROOF_REF = 0x12,
    MMB_PROOF_DUMMY = 0x13, MMB_PROOF_THM = 0x14, MMB_PROOF_THM_SAVE = 0x15,
    MMB_PROOF_HYP = 0x16, MMB_PROOF_CONV = 0x17, MMB_PROOF_REFL = 0x18,
    MMB_PROOF_SYMM = 0x19, MMB_PROOF_CONG = 0x1A, MMB_PROOF_UNFOLD = 0x1B,
    MMB_PROOF_CONV_CUT = 0x1C, MMB_PROOF_CONV_SAVE = 0x1E, MMB_PROOF_SAVE = 0x1F,
    MMB_PROOF_SORRY = 0x20,
    /* unify stream */
    MMB_UNIFY_TERM = 0x30, MMB_UNIFY_TERM_SAVE = 0x31, MMB_UNIFY_REF = 0x32,
    MMB_UNIFY_DUMMY = 0x33, MMB_UNIFY_HYP = 0x36
};

/* Type words of mm0-c/mmb.md "Term Table": deps in bits 0-54, bit 55
 * reserved, sort in 56-62, bound in 63. */
#define MMB_TYPE_DEPS_MASK ((UINT64_C(1) << 55) - 1)
#define MMB_TYPE_RESERVED_BIT (UINT64_C(1) << 55)
#define MMB_TYPE_BOUND_BIT (UINT64_C(1) << 63)
#define MMB_TYPE_SORT(type) ((uint32_t)(((type) >> 56) & 0x7F))
#define MMB_MAX_BOUND 55u

static inline uint64_t mm0_le64(const uint8_t *p) {
    return (uint64_t)p[0] | (uint64_t)p[1] << 8 | (uint64_t)p[2] << 16 |
           (uint64_t)p[3] << 24 | (uint64_t)p[4] << 32 | (uint64_t)p[5] << 40 |
           (uint64_t)p[6] << 48 | (uint64_t)p[7] << 56;
}
static inline uint32_t mm0_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}
static inline uint16_t mm0_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

/* One decoded (cmd, data) pair.  The caller guarantees five readable bytes,
 * which mmb.md requires of every command ("at least 5 bytes prior to the
 * end of the file"). */
typedef struct { uint8_t op; uint8_t size; uint32_t data; } MmbCmd;
static inline MmbCmd mmb_cmd(const uint8_t *p) {
    MmbCmd c = {(uint8_t)(p[0] & 0x3F), 1, 0};
    switch (p[0] >> 6) {
    case 0: break;
    case 1: c.size = 2; c.data = p[1]; break;
    case 2: c.size = 3; c.data = mm0_le16(p + 1); break;
    default: c.size = 5; c.data = mm0_le32(p + 1); break;
    }
    return c;
}

/* ---- specification reading, driven by the MMB statement loop ----------- */

/* The MMB tables the specification is matched against.  Entries below the
 * running counts have been checked. */
typedef struct {
    const uint8_t *file, *end;
    uint32_t num_sorts;
    const uint8_t *sorts;          /* sort modifier bytes */
    uint32_t nterms;               /* terms declared so far */
    const uint8_t *term_table;
    uint32_t nthms;
    const uint8_t *thm_table;
} MmbTables;

typedef struct Mm0Spec Mm0Spec;

Mm0Spec *mm0_spec_new(const char *text, size_t len, Mm0Steps *steps);
void mm0_spec_free(Mm0Spec *spec);
/* Each reads notation statements up to the next declaration, which must be
 * of the given kind and match table entry `index`.  The matched names and
 * binder names are recorded in *names for the theory. */
typedef struct {
    const char *name;     /* points into the specification text */
    size_t name_len;
    const char **var_names;  /* in the spec's arena; nargs (+ dummies) */
    size_t var_count;
} Mm0SpecNames;
Mm0Status mm0_spec_sort(Mm0Spec *spec, const MmbTables *tables, uint32_t index,
                        Mm0SpecNames *names, Mm0Error *error);
Mm0Status mm0_spec_term(Mm0Spec *spec, const MmbTables *tables, uint32_t index,
                        Mm0SpecNames *names, Mm0Error *error);
Mm0Status mm0_spec_thm(Mm0Spec *spec, const MmbTables *tables, uint32_t index,
                       bool axiom, Mm0SpecNames *names, Mm0Error *error);
/* After the last MMB statement: only notation statements may remain. */
Mm0Status mm0_spec_finish(Mm0Spec *spec, const MmbTables *tables, Mm0Error *error);
/* Release per-statement memory (the names of the last match). */
void mm0_spec_release(Mm0Spec *spec);

#endif
