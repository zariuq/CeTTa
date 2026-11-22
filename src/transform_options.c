/* The transform engine's process options (CETTA_TRANSFORM_TCALC_RECEIPTS,
 * _CHECKED, _CARRIER), read by the transform engine and by the native query
 * reader; kept apart from transform_core.c so that every unit reading them
 * links without the engine. */
#include "transform_core.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    bool ready, receipts, checked, terms;
} TfProcessOptions;

static _Thread_local TfProcessOptions transform_options;
extern char **environ;

/* Process options are sampled together on first use. One environment walk
 * avoids repeating three absent-key searches on a cold firing. As with the
 * engine selector, no environment string is retained. */
static void transform_options_init(void) {
    if (transform_options.ready) return;
    TfProcessOptions options = {.receipts = true};
    unsigned seen = 0u;
    for (char **entry = environ; entry && *entry; entry++) {
        const char *name = *entry;
        static const char prefix[] = "CETTA_TRANSFORM_TCALC_";
        if (name[0] != 'C' || name[1] != 'E' ||
            strncmp(name, prefix, sizeof(prefix) - 1u) != 0) continue;
        name += sizeof(prefix) - 1u;
        if (!(seen & 1u) && strncmp(name, "RECEIPTS=", 9u) == 0) {
            options.receipts = strcmp(name + 9u, "0") != 0;
            seen |= 1u;
        } else if (!(seen & 2u) && strncmp(name, "CHECKED=", 8u) == 0) {
            options.checked = strcmp(name + 8u, "0") != 0;
            seen |= 2u;
        } else if (!(seen & 4u) && strncmp(name, "CARRIER=", 8u) == 0) {
            options.terms = strcmp(name + 8u, "terms") == 0;
            seen |= 4u;
        }
    }
    options.ready = true;
    transform_options = options;
}

bool cetta_transform_tcalc_receipts(void) {
    transform_options_init();
    return transform_options.receipts;
}

bool cetta_transform_tcalc_checking(void) {
    transform_options_init();
    return transform_options.checked;
}

bool cetta_transform_tcalc_terms(void) {
    transform_options_init();
    return transform_options.terms;
}
