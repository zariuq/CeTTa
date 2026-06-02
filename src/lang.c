#include "lang.h"
#include <ctype.h>
#include <string.h>

static bool path_has_suffix(const char *path, const char *suffix) {
    size_t path_len;
    size_t suffix_len;
    if (!path || !suffix) return false;
    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (suffix_len > path_len) return false;
    return strcmp(path + (path_len - suffix_len), suffix) == 0;
}

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
    {CETTA_LANGUAGE_RHOCALC, "rhocalc", "rhocalc", true,
     "Strict-core rho-calculus reducer to quiescence",
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

const char *cetta_syntax_name(CettaSyntaxId syntax) {
    switch (syntax) {
    case CETTA_SYNTAX_AUTO:
        return "auto";
    case CETTA_SYNTAX_METTA:
        return "metta";
    case CETTA_SYNTAX_MRHO:
        return "mrho";
    case CETTA_SYNTAX_RHO:
        return "rho";
    }
    return "unknown-syntax";
}

bool cetta_syntax_from_name(const char *name, CettaSyntaxId *out_syntax) {
    if (!name) return false;
    if (ascii_ieq(name, "auto")) {
        if (out_syntax) *out_syntax = CETTA_SYNTAX_AUTO;
        return true;
    }
    if (ascii_ieq(name, "metta")) {
        if (out_syntax) *out_syntax = CETTA_SYNTAX_METTA;
        return true;
    }
    if (ascii_ieq(name, "mrho")) {
        if (out_syntax) *out_syntax = CETTA_SYNTAX_MRHO;
        return true;
    }
    if (ascii_ieq(name, "rho")) {
        if (out_syntax) *out_syntax = CETTA_SYNTAX_RHO;
        return true;
    }
    return false;
}

CettaSyntaxId cetta_syntax_default_for_language(CettaLanguageId id) {
    return id == CETTA_LANGUAGE_RHOCALC ? CETTA_SYNTAX_MRHO
                                        : CETTA_SYNTAX_METTA;
}

CettaSyntaxId cetta_syntax_infer_for_path(CettaLanguageId id, const char *path) {
    if (id == CETTA_LANGUAGE_RHOCALC && path_has_suffix(path, ".rho")) {
        return CETTA_SYNTAX_RHO;
    }
    if (id == CETTA_LANGUAGE_RHOCALC && path_has_suffix(path, ".mrho")) {
        return CETTA_SYNTAX_MRHO;
    }
    return cetta_syntax_default_for_language(id);
}

bool cetta_language_supports_syntax(CettaLanguageId id, CettaSyntaxId syntax) {
    if (syntax == CETTA_SYNTAX_AUTO) return true;
    if (id == CETTA_LANGUAGE_RHOCALC) {
        return syntax == CETTA_SYNTAX_MRHO ||
               syntax == CETTA_SYNTAX_RHO ||
               syntax == CETTA_SYNTAX_METTA;
    }
    return syntax == CETTA_SYNTAX_METTA;
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
