#ifndef CESTA_MACRO_H
#define CESTA_MACRO_H

#include "cesta.h"

typedef struct CeMacros CeMacros;
CeMacros *ce_macros_new(const CeGrammar *grammar, const CeAtom *source, CeError *error);
void ce_macros_environment(CeMacros *macros, CeText identifiers, bool unguarded);
/* Borrow the caller's lexer for the context's C grammar; the caller frees it. */
void ce_macros_share_lexer(CeMacros *macros, CeLazyLexer *lexer);
/* Expand uses as readable structured C instead of native helper macros;
   select before loading modules. */
void ce_macros_readable(CeMacros *macros);
/* Record each readable use: rule, written arguments and the names chosen. */
void ce_macros_plan(CeMacros *macros, FILE *plan);
void ce_macros_free(CeMacros *macros);
bool ce_macros_load(CeMacros *macros, const char *path, const char *theory, CeError *error);
bool ce_macros_accepts(const CeMacros *macros, const CeAtom *atom, int sort);
unsigned ce_macros_expand(CeMacros *macros, const CeAtom *atom, int sort, int parent,
                          CeArena *arena, CeTree **out, CeAtomDecoder recurse,
                          void *context, CeError *error, size_t depth);
bool ce_macros_metadata(const CeMacros *macros, FILE *file, CeError *error);
size_t ce_macros_dependencies(const CeMacros *macros);
CeText ce_macros_dependency(const CeMacros *macros, size_t index);
size_t ce_macros_rules(const CeMacros *macros);
bool ce_macros_prelude(CeMacros *macros, size_t begin, size_t end, CeArena *arena,
                       CeText *out, CeError *error);
int ce_macro_command(int argc, char **argv);

/* Library export of one concrete instance of a constructor in rules
   [begin,end). function selects a C function; otherwise a function-like
   macro. types lists (PARAMETER TYPE) substitutions for type parameters. */
typedef struct {
    CeText name, constructor;
    const CeAtom *types;
    bool function;
    size_t offset;
    FILE *plan;   /* when set, records the instance and the structure emitted */
} CeMacroExportRequest;
typedef struct {
    CeAtom *definition;
    CeText *protected_names; size_t nprotected;
    CeText *builtins; size_t nbuiltins;
} CeMacroExport;
bool ce_macros_export(CeMacros *macros, size_t begin, size_t end, const CeMacroExportRequest *request,
                      CeArena *arena, CeMacroExport *out, CeError *error);

#endif
