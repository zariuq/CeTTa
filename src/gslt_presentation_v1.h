#ifndef CETTA_GSLT_PRESENTATION_V1_H
#define CETTA_GSLT_PRESENTATION_V1_H

/* A presentation of MeTTa atoms generated at build time from a GSLT
 * presentation module and its forms table (tools/gslt_presentation_compile_v1.c).
 *
 * Reading: a DFA lexer (BNFC longest match, reserved words) and an LALR(1)
 * parser whose reductions instantiate the forms' templates through CeTTa's
 * host projection, so atoms are built directly and grounded exactly as the
 * standard reader grounds the same words.
 *
 * Printing: the first form, in table order, whose template matches the atom
 * and whose parts print; a leaf is spelled only when reading the spelling
 * back gives the same atom.  So read(print(a)) = a whenever print succeeds.
 *
 * The tables carry the generator's certificate: the LALR(1) table has no
 * conflicts, so the grammar is unambiguous. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The build-time compiler includes only the table format. */
#ifndef GSLT_PRESENTATION_TABLES_ONLY
#include "term_universe.h"
#endif

enum {
    GSLT_PRES_TERM_EOF = 0,
    GSLT_PRES_TERM_KEYWORD = 1,
    GSLT_PRES_TERM_SYMBOL = 2,
    GSLT_PRES_TERM_TOKEN = 3
};

enum {
    GSLT_PRES_PROD_PLAIN = 0,
    GSLT_PRES_PROD_COERCION = 1,
    GSLT_PRES_PROD_LIST_EMPTY = 2,
    GSLT_PRES_PROD_LIST_ONE = 3,
    GSLT_PRES_PROD_LIST_CONS = 4,
    GSLT_PRES_PROD_TOKEN = 5,
    GSLT_PRES_PROD_START = 6
};

/* How a production's single token child reads (the forms' token-forms). */
enum {
    GSLT_PRES_LEAF_NONE = 0,
    GSLT_PRES_LEAF_VAR = 1,      /* $name: the variable name */
    GSLT_PRES_LEAF_NAME = 2,     /* a word, grounded as the standard reader grounds it */
    GSLT_PRES_LEAF_QUOTED = 3,   /* `word`: backquote escapes removed, then a word */
    GSLT_PRES_LEAF_NUMBER = 4,   /* a number word */
    GSLT_PRES_LEAF_NEGATIVE = 5, /* - number: the word "-" followed by the number */
    GSLT_PRES_LEAF_STRING = 6,   /* "...": BNFC string escapes */
    GSLT_PRES_LEAF_RAW = 7,      /* r#"..."#: the text between the fences */
    GSLT_PRES_LEAF_WORD = 8      /* an S-expression word: variable, number or symbol */
};

/* Template code, prefix order. */
enum {
    GSLT_PRES_T_ARG = 1,   /* ARG i: the i-th child */
    GSLT_PRES_T_EXPR = 2,  /* EXPR n t1..tn: the expression of n atoms */
    GSLT_PRES_T_CONS = 3,  /* CONS x l: x followed by the items of l */
    GSLT_PRES_T_RUN = 4,   /* RUN e: the statement !e */
    GSLT_PRES_T_WORD = 5,  /* WORD k: constant word k */
    GSLT_PRES_T_STRING = 6, /* STRING k: constant string k */
    GSLT_PRES_T_CHANNEL = 7, /* CHANNEL k: the one atom every channel of
                                category k names in the enclosing scope */
    GSLT_PRES_T_FIXED = 8   /* FIXED i k t: child i must be the constant word k
                               (reading refuses any other); the atom is t's */
};

#define GSLT_PRES_MAX_CHANNELS 8

enum {
    GSLT_PRES_SPACING_DEFAULT = 0,
    GSLT_PRES_SPACING_TIGHT = 1, /* no space on either side: chan!(x) */
    GSLT_PRES_SPACING_SPACED = 2, /* a keyword keeps its space before "(": for (x) */
    GSLT_PRES_SPACING_PREFIX = 3  /* no space after: @{ p } */
};

/* View guards: when a form may print an atom besides its template. */
enum {
    GSLT_PRES_GUARD_NONE = 0,
    GSLT_PRES_GUARD_CALLABLE_HEAD = 1, /* the head is an expression, a symbol or
                                          an operation, not a variable or literal */
    GSLT_PRES_GUARD_SYMBOL_HEAD = 2    /* the head is a symbol */
};

/* The role of a position (only Atom and (Data T) hold, everything else
 * runs).  A position RUNs (a computation: an expression
 * there is a call), holds DATA (an expression there is inert), or holds a
 * LIST whose items run (superpose's list).  HEAD is a definition's head: a
 * call pattern, whose arguments are data.  Inside data everything is data.
 * Printing writes each expression in the look of its position's role;
 * reading is position-free, so every look denotes the same atom. */
enum {
    GSLT_PRES_ROLE_RUN = 0,
    GSLT_PRES_ROLE_DATA = 1,
    GSLT_PRES_ROLE_LIST = 2,
    GSLT_PRES_ROLE_HEAD = 3,
    GSLT_PRES_ROLE_INHERIT = 4,
    /* (forms only) a call's argument list: each argument's role is its
       position's in the head's declaration (Atom holds), RUN otherwise */
    GSLT_PRES_ROLE_ARGS = 5,
    /* the sources of a step (match, transform): passed down to its items */
    GSLT_PRES_ROLE_SOURCE = 6,
    /* (forms only) a source item's pattern: data among a step's sources, the
       parent's role elsewhere (a conjunction written where it runs) */
    GSLT_PRES_ROLE_PATTERN = 7
};
/* the roles a production may print in (its look): a bit per role, 0 any */
#define GSLT_PRES_LOOK_BIT(role) (1u << (role))

/* The role of a position declared with type T, one rule for a program's own
 * declarations (: f (-> T1 ... Tn R)) and for the builtins' declarations the
 * presentation compiler reads: Atom and (Data X) hold (data), every other
 * type runs.  HEAD is T's head symbol (T itself when
 * T is a symbol), ARITY T's length as an expression (0 for a symbol). */
static inline uint8_t gslt_presentation_v1_role_of_type(const char *head, size_t head_len, size_t arity) {
    if (!head) return GSLT_PRES_ROLE_RUN;
    if (arity == 0 && head_len == 4 && head[0] == 'A' && head[1] == 't' && head[2] == 'o' && head[3] == 'm')
        return GSLT_PRES_ROLE_DATA;
    if (arity == 2 && head_len == 4 && head[0] == 'D' && head[1] == 'a' && head[2] == 't' && head[3] == 'a')
        return GSLT_PRES_ROLE_DATA;
    return GSLT_PRES_ROLE_RUN;
}

enum {
    GSLT_PRES_RULE_SYMBOL = 0,
    GSLT_PRES_RULE_TOKEN = 1,
    GSLT_PRES_RULE_TRIVIA = 2
};

typedef struct {
    const char *name;
    /* the presentation's version, MAJOR.MINOR: MAJOR changes when a text
       that an earlier version read stops being read or reads as other
       atoms; MINOR changes when texts it refused become readable */
    const char *version;
    const char *certificate;
    const char *source_digest;
    const char *generator;

    uint16_t n_terminals;
    uint16_t n_nonterminals;
    uint16_t n_states;
    uint16_t n_productions;
    uint16_t eof_terminal;
    const char *const *terminal_text;
    const uint8_t *terminal_kind;
    const char *const *nonterminal_name;

    /* action[state * n_terminals + t]: 0 error, v > 0 shift to v - 1,
       v < 0 reduce production -v - 1; GSLT_PRES_ACCEPT accepts. */
    const int16_t *action;
    /* goto_[state * n_nonterminals + n]: -1 none */
    const int16_t *goto_;

    const uint16_t *prod_lhs;
    const uint8_t *prod_len;
    const uint32_t *prod_rhs_off;
    /* rhs symbol: t >= 0 terminal, -(n + 1) nonterminal n */
    const int16_t *prod_rhs;
    const uint8_t *prod_kind;
    const uint8_t *prod_leaf;
    const int32_t *prod_template;
    const uint8_t *prod_guard;
    const char *const *prod_label;
    /* roles of each production's children, by argument: prod_role_off[p]
       indexes role_code (one GSLT_PRES_ROLE_* per argument), or -1 when
       every child inherits; prod_look[p] is the roles it prints in */
    const int32_t *prod_role_off;
    const uint8_t *role_code;
    const uint8_t *prod_look;
    /* the positions of builtin operations, derived from their declared
       types: signature k is the head sig_head[k] with sig_len[k] arguments,
       whose roles are at sig_off[k] in role_code; a call matches a signature
       of its head with as many arguments, and a document's own
       (: f (-> ...)) declarations come first */
    uint16_t n_sigs;
    const char *const *sig_head;
    const uint32_t *sig_off;
    const uint8_t *sig_len;

    const int32_t *tcode;
    uint32_t n_tcode;
    const char *const *tconst;
    const uint32_t *tconst_len;
    uint16_t n_tconst;

    /* printing */
    const uint16_t *forms;
    uint16_t n_forms;
    const int16_t *nt_family;
    const int16_t *nt_level;
    const uint8_t *nt_is_list;
    const int16_t *nt_list_empty;
    const int16_t *nt_list_one;
    const int16_t *nt_list_cons;
    const int16_t *family_paren;
    uint16_t n_families;
    int16_t leaf_family;
    int16_t statement_nt;
    int16_t entry_nt;
    const char *statement_end;
    /* the token production read for each leaf kind, or -1 */
    int16_t leaf_prod[9];

    /* Channel scopes: a terminal may open a scope that the production it
       begins closes; inside it, every reduction of a channel category k must
       denote one atom, which the closing production's template names as
       CHANNEL k.  Printing writes that atom wherever a channel item of
       category k is not given by the template. */
    uint16_t n_channels;
    const int8_t *nt_channel;      /* channel category index, or -1 */
    const uint8_t *terminal_scope; /* opens a scope */
    const uint8_t *prod_scope;     /* closes the scope its first terminal opened */

    /* printing layout of terminals: GSLT_PRES_SPACING_* (layout never
       changes the atoms) */
    const uint8_t *terminal_spacing;
    /* terminals after which a brace group too long for its line breaks */
    const uint8_t *terminal_break;

    /* BNFC layout, lowered from the module's layout declarations: words
       after which an implicit block opens (1) or that close one (2); the
       document as one implicit block (toplevel); the block terminals. */
    bool has_layout;
    bool layout_toplevel;
    const uint8_t *terminal_layout;
    int16_t layout_open, layout_sep, layout_close;

    /* lexing */
    uint16_t n_classes;
    const uint16_t *class_of_low; /* code points below 256 */
    uint16_t n_high;
    const uint32_t *high_lo;      /* code points from 256: sorted intervals */
    const uint32_t *high_hi;
    const uint16_t *high_class;
    uint16_t n_dfa;
    const int16_t *dfa_next;      /* n_dfa * n_classes, -1 dead */
    const int16_t *dfa_accept;    /* rule or -1 */
    uint16_t n_rules;
    const uint8_t *rule_kind;
    const int16_t *rule_terminal;
    uint16_t n_reserved;
    const char *const *reserved_text;
    const int16_t *reserved_terminal;
    uint16_t n_line_comments;
    const char *const *line_comment;
    uint16_t n_block_comments;
    const char *const *block_open;
    const char *const *block_close;
} GSLTPresentationTablesV1;

#define GSLT_PRES_ACCEPT INT16_MAX

#ifndef GSLT_PRESENTATION_TABLES_ONLY
/* The presentation compiled into this executable under NAME, or NULL. */
const GSLTPresentationTablesV1 *gslt_presentation_v1_find(const char *name);
/* Names of the compiled presentations, comma separated. */
const char *gslt_presentation_v1_names(void);
/* The I-th compiled presentation, or NULL past the last. */
const GSLTPresentationTablesV1 *gslt_presentation_v1_at(size_t i);

/* Whose word grounding a presentation reads with: the dialect's own, so a
 * word means in every presentation what it means in the standard one. */
typedef enum {
    GSLT_PRES_GROUNDING_HOST = 0, /* CeTTa's host projection (HE, Prime) */
    GSLT_PRES_GROUNDING_PETTA = 1 /* the PeTTa reader's grounding */
} GSLTPresentationGroundingKindV1;

/* Read a whole document into top-level AtomIds in the order the standard
 * reader produces them: a statement !e is the word ! followed by e.
 * Returns the number of atoms, or -1 with a positioned diagnosis. */
int gslt_presentation_v1_read_ids(const GSLTPresentationTablesV1 *tables,
                                  GSLTPresentationGroundingKindV1 grounding,
                                  const char *text, size_t len,
                                  TermUniverse *universe, AtomId **out_ids,
                                  char *error_buf, size_t error_buf_size);

/* Print top-level atoms (as the standard reader returns them) in the
 * presentation.  Returns malloc'd text, or NULL with a diagnosis naming the
 * first atom no view prints. */
char *gslt_presentation_v1_print_ids(const GSLTPresentationTablesV1 *tables,
                                     GSLTPresentationGroundingKindV1 grounding,
                                     TermUniverse *universe,
                                     const AtomId *ids, size_t n,
                                     char *error_buf, size_t error_buf_size);

/* Comments are not atoms, so the laws do not speak of them; a conversion
 * keeps them as text.  Each comment is kept before the statement that
 * follows it (a comment inside a statement moves before the next one);
 * BEFORE is the index of that statement's first atom, or the number of
 * atoms for a comment after the last statement. */
typedef struct {
    size_t before;
    char *text; /* the comment's text without its delimiters */
} GSLTPresentationCommentV1;

int gslt_presentation_v1_read_ids_comments(const GSLTPresentationTablesV1 *tables,
                                           GSLTPresentationGroundingKindV1 grounding,
                                           const char *text, size_t len,
                                           TermUniverse *universe, AtomId **out_ids,
                                           GSLTPresentationCommentV1 **comments,
                                           size_t *n_comments,
                                           char *error_buf, size_t error_buf_size);
char *gslt_presentation_v1_print_ids_comments(const GSLTPresentationTablesV1 *tables,
                                              GSLTPresentationGroundingKindV1 grounding,
                                              TermUniverse *universe,
                                              const AtomId *ids, size_t n,
                                              const GSLTPresentationCommentV1 *comments,
                                              size_t n_comments,
                                              char *error_buf, size_t error_buf_size);
void gslt_presentation_v1_comments_free(GSLTPresentationCommentV1 *comments, size_t n);
/* Whether printing may write keyword-spelled symbols bare where they read
 * back as names (the compiled readers' soft keywords; the run-time route
 * reads only the backquoted spelling).  On by default. */
void gslt_presentation_v1_set_soft_keywords(bool enabled);

/* The lint of looks: read TEXT and give each expression its position's role
 * as the printer gives it; report every look its role does not admit (data
 * f{a} or a list [f, a] written where the position runs, which is a call, and
 * a call f(a) written where data or a list is held), one line each, with its
 * position.  Reading is position-free, so a wrong look never changes the
 * atoms; it misleads the reader.  Returns the number of findings, *REPORT
 * the malloc'd lines; -1 with a diagnosis when the text does not read. */
int gslt_presentation_v1_check_looks(const GSLTPresentationTablesV1 *tables,
                                     GSLTPresentationGroundingKindV1 grounding,
                                     const char *text, size_t len, TermUniverse *universe,
                                     char **report, char *error_buf, size_t error_buf_size);

/* A comment in the standard presentation: "; text" lines. */
void gslt_presentation_v1_put_metta_comment(const char *text, void *file);

/* A canonical, kind-tagged view of atoms, one statement per line, for
 * comparing readers: (var "n") (sym "s") (str "...") (gnd "text"), and
 * (run e) for !e.  Returns malloc'd text. */
char *gslt_presentation_v1_canonical_ids(TermUniverse *universe,
                                         const AtomId *ids, size_t n);
#endif /* GSLT_PRESENTATION_TABLES_ONLY */

#endif
