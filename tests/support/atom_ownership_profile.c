/* The standalone ownership probe links no evaluator. Select its PeTTa value
 * profile explicitly rather than relying on the linker's undefined weak refs. */
#include "lang.h"
#include <stdbool.h>

CettaLanguageId eval_current_language_id(void) {
    return CETTA_LANGUAGE_PETTA;
}

bool eval_current_uses_rust_he_compat_semantics(void) {
    return false;
}
