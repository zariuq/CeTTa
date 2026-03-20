#ifndef CETTA_COMPILE_H
#define CETTA_COMPILE_H

#include "atom.h"
#include "space.h"
#include <stdio.h>

/* ── Equation Compiler: MeTTa → LLVM IR ───────────────────────────────── */

/* Analyze equations in space, group by head symbol, emit LLVM IR.
   Writes LLVM IR text to the given FILE*. */
void compile_space_to_llvm(Space *s, Arena *a, FILE *out);

#endif /* CETTA_COMPILE_H */
