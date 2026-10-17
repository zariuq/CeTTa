#ifndef CETTA_MEGALODON_SEXPR_H
#define CETTA_MEGALODON_SEXPR_H

/*
 * A small S-expression reader for:
 *   - upstream megalodon `-sexprinfo` exports (the source-export reference;
 *     no OCaml runs at check time),
 *   - the Lean wire syntax of NativeSyntaxWire / NativeCursorWire
 *     (portable evidence and C tests).
 * Decoding constructs untrusted syntax only; nothing here grants authority.
 */

#include "mg_core.h"
#include "mg_session.h"

typedef enum {
    MG_SX_SYMBOL = 1,
    MG_SX_STRING,
    MG_SX_INTEGER,
    MG_SX_LIST,
} MgSxKind;

typedef struct MgSx MgSx;
struct MgSx {
    MgSxKind kind;
    uint32_t len;           /* text length or child count */
    const char *text;       /* SYMBOL / STRING (NUL-terminated copy) */
    uint64_t value;         /* INTEGER (non-negative) */
    MgSx **items;           /* LIST */
    uint64_t line;
};

typedef struct MgSxArena MgSxArena;
MgSxArena *mg_sx_arena_new(void);
void mg_sx_arena_reset(MgSxArena *arena);
void mg_sx_arena_free(MgSxArena *arena);

typedef struct {
    const char *text;
    size_t len;
    size_t pos;
    uint64_t line;
    const char *error;
} MgSxReader;

void mg_sx_reader_init(MgSxReader *r, const char *text, size_t len);
/* The next top-level form, or NULL at end (r->error set on syntax error). */
MgSx *mg_sx_read(MgSxReader *r, MgSxArena *arena);

static inline bool mg_sx_is(const MgSx *x, const char *symbol) {
    if (!x || x->kind != MG_SX_SYMBOL)
        return false;
    size_t i = 0;
    for (; symbol[i]; ++i)
        if (i >= x->len || x->text[i] != symbol[i])
            return false;
    return i == x->len;
}

static inline bool mg_sx_head(const MgSx *x, const char *symbol) {
    return x && x->kind == MG_SX_LIST && x->len >= 1u &&
           mg_sx_is(x->items[0], symbol);
}

/* Lean wire syntax (NativeSyntaxWire). */
MgTp *mg_sx_wire_tp(MgStore *s, const MgSx *x);
MgTm *mg_sx_wire_tm(MgStore *s, const MgSx *x);
MgPf *mg_sx_wire_pf(MgStore *s, const MgSx *x);

/* NativeCursorWire: contexts, judgments, witnesses and rows.
 *   (Megalodon.context D (types T0 T1 ..) (hypotheses H0 H1 ..))
 *   (Megalodon.judgment CONTEXT P)
 *   (Megalodon.hyp I) (Megalodon.known "NAME") (Megalodon.termApp M T B)
 *   (Megalodon.proofApp D) (Megalodon.termLam) (Megalodon.proofLam S)
 *   (Megalodon.typeApp T B) (Megalodon.typeLam)
 *   (Megalodon.row JUDGMENT WITNESS (parents A ..) ORIGIN)
 * Lists are innermost first: T0 is the type of term index 0.  Decoding
 * constructs untrusted syntax; the SourceCursor guards run separately. */
MgCtx *mg_sx_wire_context(MgStore *s, const MgSx *x);
bool mg_sx_wire_judgment(MgStore *s, const MgSx *x, MgJudgment *out);
bool mg_sx_wire_witness(MgStore *s, const MgSx *x, MgWitness *out);
/* The parents go to *parents, grown with mg_sys_realloc and owned by the
 * caller; out->parents points into it.  False with *nomem set when the
 * buffer could not grow. */
bool mg_sx_wire_row(MgStore *s, const MgSx *x, MgRow *out,
                    uint64_t **parents, uint32_t *parents_cap, bool *nomem);

/* Upstream -sexprinfo syntax.  `levels` is the number of declaration type
 * parameters in scope: front-end TPVAR i (a level) becomes Mathdata
 * TpVar (levels - i - 1) (syntax.ml to_mathdata_tp).  NULL on bad data. */
MgTp *mg_sx_upstream_tp(MgStore *s, const MgSx *x, uint32_t levels);
MgTm *mg_sx_upstream_tm(MgStore *s, const MgSx *x, uint32_t levels);
MgPf *mg_sx_upstream_pf(MgStore *s, const MgSx *x, uint32_t levels);

#endif /* CETTA_MEGALODON_SEXPR_H */
