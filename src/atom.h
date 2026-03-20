#ifndef CETTA_ATOM_H
#define CETTA_ATOM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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
    GV_SPACE,
    GV_STATE
} GroundedKind;

/* ── Atom ───────────────────────────────────────────────────────────────── */

typedef struct Atom Atom;
struct Atom {
    AtomKind kind;
    union {
        const char *name;   /* ATOM_SYMBOL or ATOM_VAR */
        struct {            /* ATOM_GROUNDED */
            GroundedKind gkind;
            union { int64_t ival; double fval; const char *sval; bool bval; void *ptr; };
        } ground;
        struct {            /* ATOM_EXPR */
            Atom **elems;
            uint32_t len;
        } expr;
    };
};

/* ── Arena allocator ────────────────────────────────────────────────────── */

#define ARENA_BLOCK_SIZE (64 * 1024)

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t used;
    char data[ARENA_BLOCK_SIZE];
} ArenaBlock;

typedef struct {
    ArenaBlock *head;
} Arena;

void *cetta_malloc(size_t size);
void *cetta_realloc(void *ptr, size_t size);
void  arena_init(Arena *a);
void  arena_free(Arena *a);
void *arena_alloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *s);

/* ── Symbol Interning (all equal symbols share one pointer) ────────────── */

#define INTERN_TABLE_SIZE 4096

typedef struct {
    const char **names;
    uint32_t size, used;
} InternTable;

void    intern_init(InternTable *t);
void    intern_free(InternTable *t);
/* Returns interned pointer — same string always returns same pointer */
const char *intern(InternTable *t, const char *name);

/* Global intern table (set up in main) */
extern InternTable *g_intern;

/* ── Constructors ───────────────────────────────────────────────────────── */

Atom *atom_symbol(Arena *a, const char *name);
Atom *atom_var(Arena *a, const char *name);
Atom *atom_int(Arena *a, int64_t val);
Atom *atom_float(Arena *a, double val);
Atom *atom_bool(Arena *a, bool val);
Atom *atom_string(Arena *a, const char *val);
Atom *atom_space(Arena *a, void *space_ptr);

/* State cell: holds a mutable value + its content type */
typedef struct {
    Atom *value;
    Atom *content_type; /* for (StateMonad τ) */
} StateCell;

Atom *atom_state(Arena *a, StateCell *cell);
Atom *atom_expr(Arena *a, Atom **elems, uint32_t len);
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

/* ── Predicates ─────────────────────────────────────────────────────────── */

bool atom_is_symbol(Atom *a, const char *name);
bool atom_is_empty(Atom *a);
bool atom_is_error(Atom *a);
bool atom_is_empty_or_error(Atom *a);
bool atom_is_var(Atom *a);
bool atom_is_expr(Atom *a);

/* ── Comparison ─────────────────────────────────────────────────────────── */

bool atom_eq(Atom *a, Atom *b);

/* ── Printing ───────────────────────────────────────────────────────────── */

void atom_print(Atom *a, FILE *out);
/* Print into arena-allocated string */
char *atom_to_string(Arena *a, Atom *atom);

/* Deep-copy an atom tree into a different arena */
Atom *atom_deep_copy(Arena *dst, Atom *src);

#endif /* CETTA_ATOM_H */
