#ifndef CETTA_RUNTIME_PRELUDE_H
#define CETTA_RUNTIME_PRELUDE_H
#include "space.h"
#include "session.h"

/* Shared CLI/embedding initialization, under the host's active name domain. */
bool cetta_runtime_load_prelude(Space *, Arena *, CettaLanguageId,
                               const CettaProfile *);
#endif
