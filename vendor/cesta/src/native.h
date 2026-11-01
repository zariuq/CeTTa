#ifndef CESTA_NATIVE_H
#define CESTA_NATIVE_H

#include "cesta.h"

/* Shared compiler response syntax and nested file inventory. Unreadable files
   remain explicit inputs; recursive files are refused. */
typedef struct { char **words; size_t nwords; CeText *files; size_t nfiles; } CeNativeArguments;
bool ce_native_arguments(CeArena *arena, size_t argc, char **argv,
                         CeNativeArguments *out, CeError *error);

bool ce_native_document(const CeAtom *atom);
/* With lines, the text carries #line directives naming `identity`, and the
   source map relates generated positions to source offsets. */
typedef struct {
    CeText text; CeText *dependencies; size_t ndependencies;
    const CeMapToken *tokens; size_t ntokens;
    const CeMapDirective *directives; size_t ndirectives;
    /* Source dependencies alone do not require a compiler macro-name scan. */
    bool needs_macro_inventory;
    const CeAtom *ownership;
} CeNativeOutput;
/* lexer, when present, is lent for printing this grammar across renderings. */
/* plan receives readable macro records or checked byte-buffer projections. */
typedef struct { CeText identifiers; bool unguarded, readable, lines; CeLazyLexer *lexer; FILE *plan; } CeNativeEnvironment;
bool ce_native_text_at(const CeGrammar *grammar, CeArena *arena, CeText source,
                       const char *presentation, const char *identity,
                       const CeNativeEnvironment *environment,
                       CeNativeOutput *out, CeError *error);
bool ce_native_text(const CeGrammar *grammar, CeArena *arena, CeText source,
                     const char *presentation, CeText *out, CeError *error);
/* Grammar sorts at which the compile path may start a #line directive. */
const unsigned char *ce_native_line_sorts(const CeGrammar *grammar, CeArena *arena);
bool ce_native_map_copy(CeArena *arena, const CeSourceMap *map, CeNativeOutput *out);
/* Generated C lives with the build outputs it serves: .cesta-generated beside
   OUTPUT (an object, program or dependency file, possibly in the current
   directory). With no output file (NULL), it lives in the user's cache, since
   nothing compiled from it records its path. CESTA_BUILD_DIR overrides both. */
const char *ce_generated_cache(CeArena *arena, const char *output);
int ce_expand_command(int argc, char **argv);
int ce_export_c_command(int argc, char **argv);
bool ce_native_prepare(const CeGrammar *grammar, CeArena *arena, CeText source,
                        const char *presentation, const char *identity,
                        size_t argc, char **compiler_arguments, const char *cache,
                        CeNativeOutput *out, CeError *error);
/* As ce_native_prepare, with macro uses written as readable structured C. */
bool ce_native_export(const CeGrammar *grammar, CeArena *arena, CeText source,
                      const char *presentation, const char *identity,
                      size_t argc, char **compiler_arguments, const char *cache, FILE *plan,
                      CeNativeOutput *out, CeError *error);

#endif
