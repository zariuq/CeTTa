#ifndef CESTA_PP_H
#define CESTA_PP_H

#include "cesta.h"

/* A source file read through translation phases 1-3 and the directive
   structure of 6.10. Every byte of the file belongs to exactly one token:
   its trivia (white space, comments and line splices before it) or its
   spelling. Markers are zero-width grammar terminals. */
enum {
    CE_PP_MARKER, CE_PP_HEADER_NAME, CE_PP_IDENTIFIER, CE_PP_NUMBER,
    CE_PP_CHARACTER, CE_PP_STRING, CE_PP_PUNCTUATOR, CE_PP_OTHER,
    CE_PP_UNTERMINATED, CE_PP_RAW_STRING, CE_PP_NEWLINE
};

/* The directive a line's # introduces, recorded on that # token. */
enum {
    CE_DIRECTIVE_NONE, CE_DIRECTIVE_IF, CE_DIRECTIVE_IFDEF, CE_DIRECTIVE_IFNDEF,
    CE_DIRECTIVE_ELIF, CE_DIRECTIVE_ELIFDEF, CE_DIRECTIVE_ELIFNDEF, CE_DIRECTIVE_ELSE,
    CE_DIRECTIVE_ENDIF, CE_DIRECTIVE_INCLUDE, CE_DIRECTIVE_INCLUDE_NEXT, CE_DIRECTIVE_IMPORT,
    CE_DIRECTIVE_EMBED, CE_DIRECTIVE_DEFINE, CE_DIRECTIVE_UNDEF, CE_DIRECTIVE_LINE,
    CE_DIRECTIVE_ERROR, CE_DIRECTIVE_PRAGMA, CE_DIRECTIVE_WARNING, CE_DIRECTIVE_IDENT,
    CE_DIRECTIVE_SCCS, CE_DIRECTIVE_ASSERT, CE_DIRECTIVE_UNASSERT,
    CE_DIRECTIVE_NULL, CE_DIRECTIVE_NON, CE_DIRECTIVE_LINE_MARKER
};

typedef struct {
    int symbol;                 /* grammar terminal */
    unsigned kind;              /* CE_PP_* */
    size_t trivia, begin, end;  /* trivia is [trivia, begin), spelling [begin, end) */
    CeText value;               /* the spelling after phases 1 and 2 */
    unsigned line, column;      /* physical position of the spelling, from 1 */
    bool space_before;          /* white space precedes it on its logical line */
    unsigned char directive;    /* on a directive's #: CE_DIRECTIVE_* */
    bool lenient;               /* on a directive's #: it lacks its required form */
} CeSourceToken;

typedef struct {
    CeText bytes;
    bool trigraphs, raw_strings;
    CeSourceToken *tokens;
    size_t ntokens;
    CeTokens parse;
    CeForest forest;
    CeTree *tree;
    size_t lines, directives;
} CeSourceFile;

/* The phase-4 directive grammar: ISO 6.10 with the GNU and C23 extensions. */
bool ce_source_grammar(CeGrammar *grammar, CeText cf, CeError *error);
/* c11 reads trigraphs; gnu11 reads GNU raw string literals. A scan
   classifies every line and checks conditional nesting; a read also parses
   the file with the directive grammar. */
bool ce_source_scan(const CeGrammar *grammar, CeArena *arena, CeText bytes, bool c11,
                    CeSourceFile *out, CeError *error);
bool ce_source_read(const CeGrammar *grammar, const CeLR *lr, CeArena *arena,
                    CeText bytes, bool c11, CeSourceFile *out, CeError *error);
void ce_source_free(CeSourceFile *file);
/* Whether text spells exactly one preprocessing token: its CE_PP_* kind, or 0. */
unsigned ce_source_token_kind(CeArena *arena, CeText text, bool c11);
/* The exact document: the parse with every terminal's trivia and spelling. */
bool ce_source_encode(const CeGrammar *grammar, const CeSourceFile *file, FILE *out);
/* The bytes an exact document spells, in order. */
bool ce_source_bytes(CeArena *arena, CeText document, CeText *out, CeError *error);
int ce_pp_source_command(int argc, char **argv, CeText cf);
/* Provenance: where each token of the preprocessed text was spelled, and
   the macro expansions that produced it, innermost first. */
typedef struct CePPExpansion {
    CeText macro;                     /* the macro replaced */
    const char *definition_file;      /* where it was defined */
    unsigned definition_line;
    const char *file;                 /* where it was invoked */
    unsigned line, column;
    const struct CePPExpansion *parent;
} CePPExpansion;
typedef struct {
    size_t offset, length;            /* in the preprocessed text */
    const char *file;                 /* where the token was spelled */
    unsigned line, column;
    const CePPExpansion *expansion;   /* the replacement list it came from; NULL for source tokens */
    const CePPExpansion *argument;    /* the expansion it was last substituted into as an argument */
    bool pasted;                      /* formed by ##: spelled by no single place in a file */
} CePPOrigin;
typedef struct { const char *file; bool system; } CePPDependency;
typedef struct {
    CeArena arena;                    /* owns the text, the origins and their names */
    CeText text;
    CeText predefined;                 /* selected compiler/target's predefined macros */
    CePPOrigin *origins;
    size_t len;
    bool clang;                       /* the compiler's dialect */
    CePPDependency *dependencies;      /* actually read files, including empty headers */
    size_t ndependencies;
} CePPResult;

/* Translation phase 4 as the configured compiler performs it: argv holds the
   compiler's flags and the source (with --cc COMPILER first, as for cesta
   pp). Without line markers the text is what a C reader takes. */
bool ce_pp_run(CeText cf, int argc, char **argv, bool markers, CePPResult *out, CeError *error);
/* The argv source path supplies identity and include context; its main-file
   contents are supplied here by a lossless document rather than read from disk. */
bool ce_pp_run_source(CeText cf, int argc, char **argv, CeText source,
                      bool markers, CePPResult *out, CeError *error);
void ce_pp_result_free(CePPResult *result);
/* The origin of the token at a byte offset of the preprocessed text. */
const CePPOrigin *ce_pp_origin(const CePPResult *result, size_t offset);
/* Print a located diagnostic and its macro chain. At a token boundary or EOF,
   the preceding token anchors the diagnostic. False means no origin exists. */
bool ce_pp_diagnostic(FILE *file, const CePPResult *result, const CeError *error);
/* cesta pp: translation phase 4 as the configured compiler performs it. */
int ce_pp_command(int argc, char **argv, CeText cf);

#endif
