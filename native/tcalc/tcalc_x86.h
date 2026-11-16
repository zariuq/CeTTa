/* tcalc_x86.h - the flat decider of backend x86-flat: firings decided by the
 * linked flat_fire runtime image (native/tcalc/x86/SOURCES says what of it is
 * proved). */
#ifndef CETTA_TCALC_X86_H
#define CETTA_TCALC_X86_H

#include "tcalc_core.h"

/* Whether this binary carries the runtime image. */
bool tcalc_x86_available(void);

/* A TcFlatDecider (tcalc_core.h) through ordinary_flat_adapter. */
uint64_t tcalc_x86_decide(const uint32_t *source, size_t source_words,
                          const uint32_t *premises, size_t npremises,
                          const uint32_t *constants, size_t nconstants,
                          const uint32_t *guards, size_t nguards,
                          const uint32_t *makes, size_t nmakes, uint32_t *out);

#endif
