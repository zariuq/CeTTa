#ifndef CETTA_RADIX_DIGIT_C_EMITTER_V1_H
#define CETTA_RADIX_DIGIT_C_EMITTER_V1_H

#include "radix_digit_target_program_v1.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Structurally transpile one target-owned RadixDigitMachine program and its
 * finite relation tables to standalone ordinary C.  Machine jumps remain
 * explicit C labels because they are constructors of the authored target
 * language.  The output does not link the reference evaluator.
 */
bool cetta_radix_digit_v1_emit_c(
    FILE *output,
    uint32_t radix,
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program);

#endif /* CETTA_RADIX_DIGIT_C_EMITTER_V1_H */
