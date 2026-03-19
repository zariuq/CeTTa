#include "lang.h"
#include <ctype.h>
#include <string.h>

static const CettaLanguageSpec CETTA_LANGUAGES[] = {
    {"he", "mettahe", true, "Current direct MeTTa/HE evaluator"},
    {"mettahe", "mettahe", true, "Current direct MeTTa/HE evaluator"},
    {"petta", "petta", false, "Planned dialect adapter over the shared C kernel"},
    {"ambient", "ambient", false, "Planned port from mettail-rust inventory"},
    {"calculator", "calculator", false, "Planned port from mettail-rust inventory"},
    {"imp", "imp", false, "Planned port from mettail-rust inventory"},
    {"lambda", "lambda", false, "Planned port from mettail-rust inventory"},
    {"mettafull-legacy", "mettafull-legacy", false, "Planned port from mettail-rust inventory"},
    {"minskylite", "minskylite", false, "Planned port from mettail-rust inventory"},
    {"mm0lite", "mm0lite", false, "Planned port from mettail-rust inventory"},
    {"pyashcore", "pyashcore", false, "Planned port from mettail-rust inventory"},
    {"rhocalc", "rhocalc", false, "Planned port from mettail-rust inventory"},
};

static bool ascii_ieq(const char *lhs, const char *rhs) {
    while (*lhs && *rhs) {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) {
            return false;
        }
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

const CettaLanguageSpec *cetta_language_lookup(const char *name) {
    size_t count = sizeof(CETTA_LANGUAGES) / sizeof(CETTA_LANGUAGES[0]);
    for (size_t i = 0; i < count; i++) {
        if (ascii_ieq(CETTA_LANGUAGES[i].name, name)) {
            return &CETTA_LANGUAGES[i];
        }
    }
    return NULL;
}

void cetta_language_print_inventory(FILE *out) {
    size_t count = sizeof(CETTA_LANGUAGES) / sizeof(CETTA_LANGUAGES[0]);
    const char *last_canonical = NULL;

    fputs("cetta language inventory:\n", out);
    for (size_t i = 0; i < count; i++) {
        const CettaLanguageSpec *spec = &CETTA_LANGUAGES[i];
        if (last_canonical && strcmp(last_canonical, spec->canonical) == 0) {
            continue;
        }
        fprintf(
            out,
            "  %-16s %s  %s\n",
            spec->canonical,
            spec->implemented ? "implemented" : "planned",
            spec->note
        );
        last_canonical = spec->canonical;
    }
}
