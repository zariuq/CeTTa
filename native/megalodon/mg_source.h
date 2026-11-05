#ifndef CETTA_MEGALODON_SOURCE_H
#define CETTA_MEGALODON_SOURCE_H

/*
 * Megalodon source (.mg) reading, for the evaluator.
 *
 * The items are those of Megalodon's parser.ml over the tokens of its
 * lexer.mll: document items, proof tactics, and terms parsed against the
 * live notation environment that the document's Infix, Postfix, Prefix and
 * Binder declarations build, scoped by Section and End.  Megalodon's syntax
 * as GSLT presentations (langdef/megalodon), compiled to C, reads them;
 * mg_gslt_reader.c makes them into the parse trees below, with Megalodon's
 * positions.  The readings are checked against Megalodon's own lexer and
 * parser (tests/support/megalodon_reference_{token,parse}_oracle.ml).
 * Nothing here elaborates or checks: a read item is untrusted syntax.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { MG_ASC_TP, MG_ASC_SET, MG_ASC_SUBEQ } MgAscKind;
typedef enum { MG_SET_MEM, MG_SET_SUBQ } MgSetInfix;
typedef enum {
    MG_PIC_POSTFIX, MG_PIC_INFIX_NONE, MG_PIC_INFIX_LEFT, MG_PIC_INFIX_RIGHT,
} MgPicase;

/* Megalodon's ltree. */
typedef enum {
    MG_L_BYTE = 1, MG_L_STRING, MG_L_QSTRING, MG_L_NAME, MG_L_NUM, MG_L_LET,
    MG_L_LETM, MG_L_BINDER, MG_L_PREFIX, MG_L_POSTFIX, MG_L_INFIX,
    MG_L_IMPLICIT, MG_L_SEP, MG_L_REP, MG_L_SEPREP, MG_L_SETENUM,
    MG_L_MTUPLE, MG_L_PAREN, MG_L_IF,
} MgLKind;

typedef struct MgL MgL;

typedef struct {
    MgAscKind kind;
    MgL *type;
} MgLAsc;

typedef struct {
    const char **names;
    uint32_t name_count;
    const MgLAsc *asc;      /* NULL when the group has no ascription */
} MgLVarGroup;

struct MgL {
    MgLKind kind;
    int32_t byte;           /* BYTE */
    bool negative;          /* NUM */
    bool set_infix;         /* INFIX: InfSet rather than InfNam */
    MgSetInfix set_op;      /* INFIX (set), SEP, REP, SEPREP */
    const char *text;       /* payload, name, operator, binder, NUM digits */
    const char *text2;      /* BINDER separator; NUM fraction */
    const char *text3;      /* NUM exponent */
    const MgLAsc *asc;      /* LET ascription */
    MgL *a, *b, *c;
    MgL **list;             /* SETENUM elements; MTUPLE and PAREN rest */
    uint32_t list_len;
    const char **names;     /* LETM names */
    uint32_t names_len;
    MgLVarGroup *groups;    /* BINDER variable groups */
    uint32_t group_count;
};

typedef enum {
    MG_DOC_AUTHOR = 1, MG_DOC_TITLE, MG_DOC_SALT, MG_DOC_OPAQUE,
    MG_DOC_TRANSPARENT, MG_DOC_TREASURE, MG_DOC_SHOWPROOFTERMS,
    MG_DOC_SECTION, MG_DOC_END, MG_DOC_VAR, MG_DOC_LET, MG_DOC_HYP,
    MG_DOC_POSTINFIX, MG_DOC_PREFIX, MG_DOC_BINDER, MG_DOC_NOTATION,
    MG_DOC_UNICODE, MG_DOC_SUBSCRIPT, MG_DOC_SUPERSCRIPT, MG_DOC_PARAM,
    MG_DOC_DEF, MG_DOC_PARAMHASH, MG_DOC_AXIOM, MG_DOC_THEOREM,
} MgDocKind;

typedef enum {
    MG_TAC_STRUCT = 1, MG_TAC_EXACT, MG_TAC_LET, MG_TAC_ASSUME, MG_TAC_SET,
    MG_TAC_APPLY, MG_TAC_CLAIM, MG_TAC_PROVE, MG_TAC_CASES, MG_TAC_WITNESS,
    MG_TAC_REWRITE, MG_TAC_SPECIAL, MG_TAC_QED, MG_TAC_ADMITTED,
    MG_TAC_ADMIT, MG_TAC_ABY,
} MgTacKind;

typedef struct {
    const char *name;
    MgL *term;
} MgNamedL;

typedef struct {
    MgNamedL *cases;
    uint32_t count;
} MgCaseGroup;

/* One read item: a document item, or a tactic while a theorem is open. */
typedef struct {
    bool tactic;
    int kind;               /* MgDocKind, or MgTacKind when tactic */
    const char *name;       /* names, strings, kinds */
    const char *name2;
    const char *name3;      /* ParamHash proof hash; NULL when absent */
    const char **names;     /* name and string lists */
    uint32_t names_len;
    MgL *a;                 /* NULL where the term is optional and absent */
    MgL *b;
    const MgLAsc *asc;      /* LetDecl ascription */
    MgAscKind asc_kind;     /* VarDecl ascription */
    int32_t priority;
    MgPicase picase;
    bool flag;              /* ShowProofTerms; Binder+; rewrite <- */
    bool flag2;             /* Binder comma separator */
    MgL **terms;            /* ProveTac and SpecialTac terms */
    uint32_t terms_len;
    int64_t *positions;     /* rewrite at positions */
    uint32_t positions_len;
    MgCaseGroup *case_groups;
    uint32_t case_group_count;
    int32_t depth;          /* PfStruct */
    uint32_t line;          /* line where the item began */
    /* The lexer's position once the item is read, where Megalodon
     * evaluates it (its lineno and charno). */
    int end_line, end_char;
} MgSourceItem;

typedef struct MgSource MgSource;

/* A reader over BYTES, which must outlive it, as must each later input;
 * with BYTES NULL, a reader whose first input mg_source_set_input gives. */
MgSource *mg_source_new(const char *bytes, size_t len);
void mg_source_free(MgSource *source);

/* Continue the same reading in another file (an -I signature, then the
 * main file): the lexer restarts, open sections are dropped without being
 * undone, and the notation environment carries over, as in Megalodon. */
void mg_source_set_input(MgSource *source, const char *bytes, size_t len);

/* The next item: 1 with *out set, 0 at the end of the document, -1 on a
 * parse error or failure (mg_source_error).  Syntax effects (Section, End,
 * notation declarations) take hold before the next item is read; a theorem
 * opens tactic mode until Qed or Admitted.  An item cut short by the
 * end of input ends the document, as in Megalodon. */
int mg_source_next(MgSource *source, const MgSourceItem **out);

/* "parse error L:C-L:C: MESSAGE" or "failure: MESSAGE", as Megalodon
 * reports it. */
const char *mg_source_error(const MgSource *source);

/* The lexer's position (Megalodon's lineno and charno). */
void mg_source_position(const MgSource *source, int *line, int *charno);

#endif
