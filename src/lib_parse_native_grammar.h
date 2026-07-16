#ifndef CETTA_LIB_PARSE_NATIVE_GRAMMAR_H
#define CETTA_LIB_PARSE_NATIVE_GRAMMAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

typedef enum {
    CETTA_LP_NATIVE_SYMBOL_TM = 0,
    CETTA_LP_NATIVE_SYMBOL_HL = 1,
    CETTA_LP_NATIVE_SYMBOL_HLB = 2
} CettaLpNativeSymbolKind;

typedef struct {
    CettaLpNativeSymbolKind kind;
    SymbolId name;
    SymbolId scope;
} CettaLpNativeSymbol;

typedef struct {
    SymbolId label;
    SymbolId lhs;
    CettaLpNativeSymbol *rhs;
    uint32_t rhs_len;
} CettaLpNativeProduction;

typedef struct {
    SymbolId atom;
    SymbolId nt;
} CettaLpNativeVarDecl;

typedef struct {
    SymbolId klass;
    SymbolId nt;
} CettaLpNativeLexDecl;

typedef enum {
    CETTA_LP_NATIVE_ENTRY_PRODUCTION = 0,
    CETTA_LP_NATIVE_ENTRY_VAR = 1,
    CETTA_LP_NATIVE_ENTRY_LEX = 2
} CettaLpNativeEntryKind;

typedef struct {
    CettaLpNativeEntryKind kind;
    uint32_t index;
} CettaLpNativeEntry;

typedef struct {
    CettaLpNativeProduction *productions;
    uint32_t production_len;
    CettaLpNativeVarDecl *vars;
    uint32_t var_len;
    CettaLpNativeLexDecl *lexes;
    uint32_t lex_len;
    CettaLpNativeEntry *entries;
    uint32_t entry_len;
    uint32_t binder_hole_count;
} CettaLpNativeGrammar;

typedef struct {
    uint32_t production_len;
    uint32_t var_len;
    uint32_t lex_len;
    uint32_t binder_hole_count;
} CettaLpNativeGrammarSummary;

typedef struct {
    uint32_t state_len;
    uint32_t shift_len;
    uint32_t goto_len;
    uint32_t reduce_len;
    uint32_t accept_len;
    uint32_t conflict_len;
} CettaLpNativeSlrSummary;

void cetta_lp_native_grammar_init(CettaLpNativeGrammar *grammar);
void cetta_lp_native_grammar_free(CettaLpNativeGrammar *grammar);

bool cetta_lp_native_grammar_load_forms(CettaLpNativeGrammar *out,
                                        Atom **forms,
                                        int form_count,
                                        const char *def_name,
                                        char *error_buf,
                                        size_t error_buf_size);

bool cetta_lp_native_grammar_load_file(CettaLpNativeGrammar *out,
                                       const char *filename,
                                       const char *def_name,
                                       char *error_buf,
                                       size_t error_buf_size);

bool cetta_lp_native_grammar_load_list(CettaLpNativeGrammar *out,
                                       Atom *grammar_list,
                                       char *error_buf,
                                       size_t error_buf_size);

void cetta_lp_native_grammar_summary(const CettaLpNativeGrammar *grammar,
                                     CettaLpNativeGrammarSummary *out);

bool cetta_lp_native_slr_summary(const CettaLpNativeGrammar *grammar,
                                 SymbolId start_nt,
                                 CettaLpNativeSlrSummary *out,
                                 char *error_buf,
                                 size_t error_buf_size);

Atom *cetta_lp_native_slr_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_glr_parse_class(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size);

Atom *cetta_lp_native_glr_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_summary(const CettaLpNativeGrammar *grammar,
                                         SymbolId start_nt,
                                         Atom *token_list,
                                         Arena *arena,
                                         char *error_buf,
                                         size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_signature(const CettaLpNativeGrammar *grammar,
                                           SymbolId start_nt,
                                           Atom *token_list,
                                           Arena *arena,
                                           char *error_buf,
                                           size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_signature_digest(const CettaLpNativeGrammar *grammar,
                                                  SymbolId start_nt,
                                                  Atom *token_list,
                                                  Arena *arena,
                                                  char *error_buf,
                                                  size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_data(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size);

Atom *cetta_lp_native_gll_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_gll_recognize(const CettaLpNativeGrammar *grammar,
                                    SymbolId start_nt,
                                    Atom *token_list,
                                    Arena *arena,
                                    char *error_buf,
                                    size_t error_buf_size);

Atom *cetta_lp_native_gll_recognize_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_span_summary(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_gll_span_summary_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_summary(const CettaLpNativeGrammar *grammar,
                                         SymbolId start_nt,
                                         Atom *token_list,
                                         Arena *arena,
                                         char *error_buf,
                                         size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_summary_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_signature(const CettaLpNativeGrammar *grammar,
                                           SymbolId start_nt,
                                           Atom *token_list,
                                           Arena *arena,
                                           char *error_buf,
                                           size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_signature_digest(const CettaLpNativeGrammar *grammar,
                                                  SymbolId start_nt,
                                                  Atom *token_list,
                                                  Arena *arena,
                                                  char *error_buf,
                                                  size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_signature_digest_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_data(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size);

#endif /* CETTA_LIB_PARSE_NATIVE_GRAMMAR_H */
