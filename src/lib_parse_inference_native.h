#ifndef CETTA_LIB_PARSE_INFERENCE_NATIVE_H
#define CETTA_LIB_PARSE_INFERENCE_NATIVE_H

#include <stddef.h>

#include "atom.h"
#include "lib_parse_native_grammar.h"

Atom *cetta_lp_native_inference_presentation(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_inference_dag(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    SymbolId start_nt,
    Atom *token_list,
    Atom *certificate,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_inference_dag_presentation(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_inference_dag_proof(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    SymbolId start_nt,
    Atom *token_list,
    Atom *certificate,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

#endif
