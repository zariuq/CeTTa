#include "lang.h"
#include <ctype.h>
#include <string.h>

static const CettaLanguageSpec CETTA_LANGUAGES[] = {
    {CETTA_LANGUAGE_HE, "he", "he", true, "Current direct MeTTa/HE evaluator",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_HE, "mettahe", "he", true, "Current direct MeTTa/HE evaluator",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_MM2, "mm2", "mm2", true,
     "Direct MM2 lowering plus MORK-backed execution for pure program files",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_PETTA, "petta", "petta", false,
     "Planned dialect adapter over the shared C kernel",
     CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK},
    {CETTA_LANGUAGE_AMBIENT, "ambient", "ambient", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_CALCULATOR, "calculator", "calculator", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_IMP, "imp", "imp", false, "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_LAMBDA, "lambda", "lambda", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_METTAFULL_LEGACY, "mettafull-legacy", "mettafull-legacy", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_MINSKYLITE, "minskylite", "minskylite", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_MM0LITE, "mm0lite", "mm0lite", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_PYASHCORE, "pyashcore", "pyashcore", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
    {CETTA_LANGUAGE_RHOCALC, "rhocalc", "rhocalc", false,
     "Planned port from mettail-rust inventory",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
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

const CettaLanguageSpec *cetta_language_from_id(CettaLanguageId id) {
    size_t count = sizeof(CETTA_LANGUAGES) / sizeof(CETTA_LANGUAGES[0]);
    for (size_t i = 0; i < count; i++) {
        if (CETTA_LANGUAGES[i].id == id &&
            strcmp(CETTA_LANGUAGES[i].name, CETTA_LANGUAGES[i].canonical) == 0) {
            return &CETTA_LANGUAGES[i];
        }
    }
    return NULL;
}

const char *cetta_language_canonical_name(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec ? spec->canonical : "unknown-language";
}

const char *cetta_relative_module_policy_name(CettaRelativeModulePolicy policy) {
    switch (policy) {
    case CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY:
        return "relative";
    case CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK:
        return "ancestor-walk";
    case CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY:
        return "upstream";
    }
    return "unknown";
}

bool cetta_relative_module_policy_from_name(const char *name,
                                            CettaRelativeModulePolicy *out_policy) {
    if (!name) {
        return false;
    }
    if (ascii_ieq(name, "relative") || ascii_ieq(name, "current-dir") ||
        ascii_ieq(name, "script-dir")) {
        if (out_policy) {
            *out_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
        }
        return true;
    }
    if (ascii_ieq(name, "ancestor-walk") || ascii_ieq(name, "walk")) {
        if (out_policy) {
            *out_policy = CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK;
        }
        return true;
    }
    if (ascii_ieq(name, "upstream") || ascii_ieq(name, "working-dir") ||
        ascii_ieq(name, "cwd")) {
        if (out_policy) {
            *out_policy = CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY;
        }
        return true;
    }
    return false;
}

CettaRelativeModulePolicy cetta_language_relative_module_policy(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    if (!spec) {
        return CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
    }
    return spec->relative_module_policy;
}

void cetta_language_print_inventory(FILE *out) {
    size_t count = sizeof(CETTA_LANGUAGES) / sizeof(CETTA_LANGUAGES[0]);
    const char *last_canonical = NULL;

    fputs("cetta language inventory (--lang driver/front-end):\n", out);
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
