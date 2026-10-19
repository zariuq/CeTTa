#ifndef CETTA_REGEX_COMPILER_V1_H
#define CETTA_REGEX_COMPILER_V1_H
#include "regex_selection_v1.h"
#include "deterministic_equation_plan_v1.h"

/* Ordinary loading restores the authored equation program. No regex source
 * grammar or equation admission is reconstructed at this boundary. */
typedef struct CettaReCompilerV1 CettaReCompilerV1;
bool cetta_re_v1_compiler_load(CettaReCompilerV1 **, char *, size_t);
bool cetta_re_v1_compiler_restore(const uint8_t *, size_t, bool audit,
                                 CettaReCompilerV1 **, char *, size_t);
bool cetta_re_v1_compiler_store(const CettaReCompilerV1 *, uint8_t **, size_t *, char *, size_t);
void cetta_re_v1_compiler_free(CettaReCompilerV1 *);
/* Source syntax is inert. Runtime callbacks only relocate symbolic graph
 * addresses and decode scalar/slot fields produced by the authored compiler.
 * word_ranges is the explicit interpretation of word-boundary assertions. */
bool cetta_re_v1_compile(CettaReCompilerV1 *, Arena *, Atom *pattern,
    const CettaReRangeV1 *word_ranges, uint32_t word_range_len,
    uint32_t depth_limit, uint64_t work_limit, CettaReProgramV1 **,
    CettaDeterministicEquationStatusV1 *, char *, size_t);
bool cetta_re_v1_compile_named(CettaReCompilerV1 *, Arena *, Atom *, const char *const *names,
    uint32_t group_len, const CettaReRangeV1 *, uint32_t, uint32_t, uint64_t,
    CettaReProgramV1 **, CettaDeterministicEquationStatusV1 *, char *, size_t);
#endif
