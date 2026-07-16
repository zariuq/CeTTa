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

#endif
