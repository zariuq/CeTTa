#ifndef CETTA_GRAMMAR_CANONICAL_TERM_V1_H
#define CETTA_GRAMMAR_CANONICAL_TERM_V1_H

#include "src/atom.h"
#include "src/lib_parse_native_grammar.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Canonical terms of a grammar derivation.
 *
 * A grammar is a list of productions.  Each right-hand-side symbol is a
 * rule, a token (a terminal carrying a lexeme) or a fixed terminal; each
 * production belongs to an authored rule or to a helper that an EBNF
 * lowering generated for a repetition, an option or a group, and each helper
 * names the authored rule that contains it.  Canonical terms are lossless:
 * a value that is not the only child of its production names the production
 * that made it.  Every production is classified once:
 *   - a production whose right-hand side is exactly one rule or token is
 *     transparent, denoting its child, where it is the one transparent path
 *     from its sort; where two transparent paths from a sort meet one rule,
 *     token class or literal text (the grammar is ambiguous there), that
 *     sort's single-child productions are constructors, so each derivation
 *     keeps its alternative;
 *   - a production made only of fixed terminals denotes their text;
 *   - every other production of an authored rule is a constructor named by
 *     its rule, with the alternative index appended when the rule has more
 *     than one constructor production; its arguments are its rule and token
 *     children in order, and fixed terminals are dropped;
 *   - a group is named by its owner and ordinal, owner.k (owner.k/i for its
 *     alternatives when it has several), and is a constructor like the
 *     others: a one-alternative group is transparent only when it is exactly
 *     one rule or token;
 *   - a repetition or an option is a node named by its owner, its operator
 *     and its ordinal among the owner's helpers of that operator (owner*1,
 *     owner+2, owner?1), whose one argument is the list of its items; an
 *     option's list has at most one; a repetition whose body has no child
 *     lists its text once per occurrence;
 *   - a rule whose two alternatives are x and x sep... R (or R sep... x),
 *     with R the rule itself and the separators fixed terminals, is a node
 *     named by the rule over the list of its x values;
 *   - a child immediately followed by a repetition whose items are that same
 *     symbol (the lowering of x+ and x (sep x)*) makes one list, named by the
 *     repetition.
 * A token is a constructor named by its token rule whose one argument is its
 * lexeme.  In MeTTa every constructor application, list node and token is
 * written (bnf NAME child...), so grammar names never stand in a head
 * position an evaluator could reduce: (bnf upper_word "X"),
 * (bnf fof_arguments (...)); a name a host reads back as a literal (True,
 * false, Empty, 1) is the string NAME.  Literals are strings; the list inside
 * a list node is an expression of its elements.  Nothing depends on how rule,
 * token or helper names are spelled.
 */

typedef struct CettaGrammarCanonicalTableV1 CettaGrammarCanonicalTableV1;

typedef enum {
    CETTA_GRAMMAR_CANONICAL_RULE_SYMBOL_V1 = 0,
    CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1 = 1,
    CETTA_GRAMMAR_CANONICAL_FIXED_SYMBOL_V1 = 2
} CettaGrammarCanonicalSymbolKindV1;

typedef struct {
    CettaGrammarCanonicalSymbolKindV1 kind;
    SymbolId name;
    const char *text; /* fixed terminals: their one text */
} CettaGrammarCanonicalSymbolV1;

typedef enum {
    CETTA_GRAMMAR_CANONICAL_AUTHORED_V1 = 0,
    CETTA_GRAMMAR_CANONICAL_REPETITION_V1 = 1,
    CETTA_GRAMMAR_CANONICAL_OPTION_V1 = 2,
    CETTA_GRAMMAR_CANONICAL_GROUP_V1 = 3
} CettaGrammarCanonicalRuleKindV1;

typedef struct {
    SymbolId label;                       /* the derivation-node label */
    SymbolId lhs;
    CettaGrammarCanonicalRuleKindV1 rule_kind;
    SymbolId owner;                       /* helpers: the authored rule containing them */
    char repeat;                          /* repetitions and options: '*', '+' or '?';
                                             0 takes '*' for a repetition, '?' for an option */
    const CettaGrammarCanonicalSymbolV1 *rhs;
    uint32_t rhs_len;
} CettaGrammarCanonicalProductionV1;

/* The lowering origin of one helper nonterminal. */
typedef struct {
    SymbolId helper;
    CettaGrammarCanonicalRuleKindV1 kind; /* repetition, option or group */
    char repeat;                          /* '*', '+' or '?' for repetitions and options */
    SymbolId owner;                       /* the authored rule containing it */
} CettaGrammarCanonicalOriginV1;

/* Classify a grammar.  Productions of one left-hand side share one rule kind
 * and appear in alternative order. */
bool cetta_grammar_canonical_table_build_v1(
    const CettaGrammarCanonicalProductionV1 *productions,
    uint32_t production_len,
    CettaGrammarCanonicalTableV1 **out,
    char *error,
    size_t error_size);

typedef struct {
    /* True when the terminal carries a lexeme (its language has more than
     * one string); otherwise terminal_fixed_text gives its one text. */
    bool (*terminal_is_value)(void *context, SymbolId terminal);
    const char *(*terminal_fixed_text)(void *context, SymbolId terminal);
    void *context;
} CettaGrammarCanonicalTerminalsV1;

/* A prepared parser program: its authored productions, and the productions
 * of the helpers its lowering origins name. */
bool cetta_grammar_canonical_table_from_slr_v1(
    const CettaLpNativeSlrProgram *slr,
    const CettaGrammarCanonicalTerminalsV1 *terminals,
    const CettaGrammarCanonicalOriginV1 *origins,
    uint32_t origin_len,
    CettaGrammarCanonicalTableV1 **out,
    char *error,
    size_t error_size);

void cetta_grammar_canonical_table_free_v1(CettaGrammarCanonicalTableV1 *table);

/* A token leaf of a derivation: its token rule and its lexeme as a string
 * atom; false when the node is not a token leaf. */
typedef bool (*CettaGrammarCanonicalLeafV1)(
    void *context, Arena *arena, const Atom *node, const char *label,
    SymbolId *token, Atom **text);

/*
 * Build the canonical term of a CstRuleV1 derivation.  A node whose label is
 * a production label is that production; any other node is a token leaf
 * resolved by leaf; nodes labelled epsilon_label contribute nothing.
 */
bool cetta_grammar_canonical_from_cst_v1(
    const CettaGrammarCanonicalTableV1 *table,
    const Atom *cst,
    const char *epsilon_label,
    CettaGrammarCanonicalLeafV1 leaf,
    void *leaf_context,
    Arena *arena,
    Atom **out,
    char *error,
    size_t error_size);

/*
 * The derived LanguageDef of the canonical terms, as a GSLTLanguageDefWireV1
 * value.  Its rows are the productions re-expressed over canonical terms:
 * constructor rows; unit rows for transparent productions and rows of
 * one-alternative groups, which canonical terms leave headless; literal
 * rows; one row per token; and vector parameters for lists.
 */
bool cetta_grammar_canonical_language_v1(
    const CettaGrammarCanonicalTableV1 *table,
    const char *name,
    Arena *arena,
    Atom **out,
    char *error,
    size_t error_size);

/* The forms a canonical value takes: each constructor with its argument
 * count, each literal text and each token (whose one argument is its
 * lexeme), in grammar order.  An interpretation of the canonical terms needs
 * exactly one case for each.  The array is malloc'd; its names belong to the
 * table. */
typedef enum {
    CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1 = 0,
    CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1 = 1,
    CETTA_GRAMMAR_CANONICAL_TOKEN_FORM_V1 = 2
} CettaGrammarCanonicalFormKindV1;

typedef struct {
    CettaGrammarCanonicalFormKindV1 kind;
    const char *name;  /* constructor or token name, or literal text */
    uint32_t arity;
    bool string_name;  /* the name is written as a string, (bnf "True") */
} CettaGrammarCanonicalFormV1;

bool cetta_grammar_canonical_forms_v1(
    const CettaGrammarCanonicalTableV1 *table,
    CettaGrammarCanonicalFormV1 **forms,
    uint32_t *form_len,
    char *error,
    size_t error_size);

/* The elements of a list node (bnf NAME (element ...)); false for any other
 * term. */
bool cetta_grammar_canonical_list_elements_v1(const Atom *term, Atom *const **elements,
                                              uint32_t *len);

/* What a language morphism may erase.  Canonical terms name every list, so
 * they are injective; this reports the sorts at which erasing names would
 * merge values.  From each left-hand side, the productions reachable through
 * transparent productions make its values: a constructor nodes of its name,
 * a literal production its text, a transparent token child tokens of its
 * class, and a repetition, option or separated-list rule lists.  The ways two
 * can coincide:
 *   sequence: two different list-making rules, whose lists alone do not say
 *             which made them (compared by kind alone);
 *   literal:  two literal productions of one text;
 *   token:    two transparent productions over one token class;
 *   path:     one rule reached along two paths.
 * The last three are ambiguities of the grammar: both paths spell the same
 * text.  Canonical terms keep such derivations apart by naming the sort's
 * unary alternatives; the report still lists each ambiguity.  The array is
 * malloc'd; its names are interned symbol spellings. */
typedef enum {
    CETTA_GRAMMAR_CANONICAL_SEQUENCE_COLLISION_V1 = 0,
    CETTA_GRAMMAR_CANONICAL_LITERAL_COLLISION_V1 = 1,
    CETTA_GRAMMAR_CANONICAL_TOKEN_COLLISION_V1 = 2,
    CETTA_GRAMMAR_CANONICAL_PATH_COLLISION_V1 = 3
} CettaGrammarCanonicalCollisionKindV1;

typedef struct {
    CettaGrammarCanonicalCollisionKindV1 kind;
    const char *sort;    /* the left-hand side both are reachable from */
    const char *first;   /* the two rules whose productions make the shape;
                            for path, the rule reached twice */
    const char *second;  /* NULL for path */
} CettaGrammarCanonicalCollisionV1;

bool cetta_grammar_canonical_collisions_v1(
    const CettaGrammarCanonicalTableV1 *table,
    CettaGrammarCanonicalCollisionV1 **collisions,
    uint32_t *collision_len,
    char *error,
    size_t error_size);

/* Whether two adjacent texts need layout between them to lex apart. */
typedef bool (*CettaGrammarCanonicalAdjacentV1)(
    void *context, const char *left, size_t left_len, const char *right, size_t right_len);

/*
 * Print a canonical term occupying a position of the given grammar symbol.
 * The result is malloc'd and owned by the caller.
 */
bool cetta_grammar_canonical_print_v1(
    const CettaGrammarCanonicalTableV1 *table,
    const Atom *term,
    SymbolId symbol,
    CettaGrammarCanonicalAdjacentV1 adjacent,
    void *adjacent_context,
    char **out,
    size_t *out_len,
    char *error,
    size_t error_size);

/*
 * Print a term whose nodes may stand where no production of their position
 * reaches them: such a node prints inside the shortest chain (at most four) of
 * wrappers that reaches it.  A wrapper is a constructor with one child named
 * in wrappers, such as the parenthesized formula, which the interpretation of
 * the terms maps to its child.  Complete terms print as by the plain printer.
 */
bool cetta_grammar_canonical_print_wrapped_v1(
    const CettaGrammarCanonicalTableV1 *table,
    const Atom *term,
    SymbolId symbol,
    const SymbolId *wrappers,
    uint32_t wrapper_len,
    CettaGrammarCanonicalAdjacentV1 adjacent,
    void *adjacent_context,
    char **out,
    size_t *out_len,
    char *error,
    size_t error_size);

#endif /* CETTA_GRAMMAR_CANONICAL_TERM_V1_H */
