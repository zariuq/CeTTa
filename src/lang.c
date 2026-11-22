/* lang.c - the languages of --lang, read from their rows in lib/profiles.data
 * (src/generated/cetta_profiles.generated.h).  Adding a language, an alias or
 * a syntax is adding a row there. */
#include "lang.h"
#include <ctype.h>
#include <string.h>

#define LANGUAGE_SPEC(code, name, implemented, note, syntax, relational, gslt, destination, \
                      named_head) \
    {code, name, implemented, note, syntax, CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY, \
     (relational) ? CETTA_PORTABLE_RELATIONAL_EAGER_STRUCTURAL_EQUATIONS_V1 \
                  : CETTA_PORTABLE_RELATIONAL_NONE, \
     gslt, (destination) ? CETTA_CALL_RESULT_BY_DESTINATION : CETTA_CALL_RESULT_BY_VALUE, \
     named_head},
static const CettaLanguageSpec CETTA_LANGUAGES[] = {CETTA_LANGUAGE_ROWS(LANGUAGE_SPEC)};
#undef LANGUAGE_SPEC

#define LANGUAGE_ALIAS(name, language) {name, language},
static const struct { const char *name; CettaLanguageId language; } CETTA_LANGUAGE_ALIASES[] = {
    CETTA_LANGUAGE_ALIAS_ROWS(LANGUAGE_ALIAS)};
#undef LANGUAGE_ALIAS

#define SYNTAX_NAME(code, name) {code, name},
static const struct { CettaSyntaxId syntax; const char *name; } CETTA_SYNTAXES[] = {
    CETTA_SYNTAX_ROWS(SYNTAX_NAME)};
#undef SYNTAX_NAME

/* The syntaxes a language reads besides its default, and the path suffix that
 * selects each ("" selects none). */
#define LANGUAGE_SYNTAX(language, syntax, suffix) {language, syntax, suffix},
static const struct {
    CettaLanguageId language; CettaSyntaxId syntax; const char *suffix;
} CETTA_LANGUAGE_SYNTAXES[] = {CETTA_LANGUAGE_SYNTAX_ROWS(LANGUAGE_SYNTAX)};
#undef LANGUAGE_SYNTAX

#define COUNT(table) (sizeof(table) / sizeof((table)[0]))

static bool ascii_ieq(const char *lhs, const char *rhs) {
    while (*lhs && *rhs) {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) return false;
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static bool path_has_suffix(const char *path, const char *suffix) {
    if (!path || !suffix || !*suffix) return false;
    size_t path_len = strlen(path), suffix_len = strlen(suffix);
    return suffix_len <= path_len && strcmp(path + (path_len - suffix_len), suffix) == 0;
}

const CettaLanguageSpec *cetta_language_lookup(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < COUNT(CETTA_LANGUAGES); i++)
        if (ascii_ieq(CETTA_LANGUAGES[i].canonical, name)) return &CETTA_LANGUAGES[i];
    for (size_t i = 0; i < COUNT(CETTA_LANGUAGE_ALIASES); i++)
        if (ascii_ieq(CETTA_LANGUAGE_ALIASES[i].name, name))
            return cetta_language_from_id(CETTA_LANGUAGE_ALIASES[i].language);
    return NULL;
}

/* The rows are written in the order of their ids, so a language is found at
 * its id; the evaluator asks these properties on its hot paths. */
const CettaLanguageSpec *cetta_language_from_id(CettaLanguageId id) {
    if ((size_t)id < COUNT(CETTA_LANGUAGES) && CETTA_LANGUAGES[id].id == id)
        return &CETTA_LANGUAGES[id];
    for (size_t i = 0; i < COUNT(CETTA_LANGUAGES); i++)   /* a row written out of order */
        if (CETTA_LANGUAGES[i].id == id) return &CETTA_LANGUAGES[i];
    return NULL;
}

const char *cetta_language_canonical_name(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec ? spec->canonical : "unknown-language";
}

const char *cetta_syntax_name(CettaSyntaxId syntax) {
    for (size_t i = 0; i < COUNT(CETTA_SYNTAXES); i++)
        if (CETTA_SYNTAXES[i].syntax == syntax) return CETTA_SYNTAXES[i].name;
    return "unknown-syntax";
}

bool cetta_syntax_from_name(const char *name, CettaSyntaxId *out_syntax) {
    if (!name) return false;
    for (size_t i = 0; i < COUNT(CETTA_SYNTAXES); i++)
        if (ascii_ieq(CETTA_SYNTAXES[i].name, name)) {
            if (out_syntax) *out_syntax = CETTA_SYNTAXES[i].syntax;
            return true;
        }
    return false;
}

CettaSyntaxId cetta_syntax_default_for_language(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec ? spec->syntax : CETTA_SYNTAX_METTA;
}

CettaSyntaxId cetta_syntax_infer_for_path(CettaLanguageId id, const char *path) {
    for (size_t i = 0; i < COUNT(CETTA_LANGUAGE_SYNTAXES); i++)
        if (CETTA_LANGUAGE_SYNTAXES[i].language == id &&
            path_has_suffix(path, CETTA_LANGUAGE_SYNTAXES[i].suffix))
            return CETTA_LANGUAGE_SYNTAXES[i].syntax;
    return cetta_syntax_default_for_language(id);
}

bool cetta_language_supports_syntax(CettaLanguageId id, CettaSyntaxId syntax) {
    if (syntax == CETTA_SYNTAX_AUTO || syntax == cetta_syntax_default_for_language(id)) return true;
    for (size_t i = 0; i < COUNT(CETTA_LANGUAGE_SYNTAXES); i++)
        if (CETTA_LANGUAGE_SYNTAXES[i].language == id && CETTA_LANGUAGE_SYNTAXES[i].syntax == syntax)
            return true;
    return false;
}

bool cetta_language_uses_embedded_gslt(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec && spec->embedded_gslt;
}

const char *cetta_relative_module_policy_name(CettaRelativeModulePolicy policy) {
    switch (policy) {
    case CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY: return "relative";
    case CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK: return "ancestor-walk";
    case CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY: return "upstream";
    }
    return "unknown";
}

bool cetta_relative_module_policy_from_name(const char *name,
                                            CettaRelativeModulePolicy *out_policy) {
    static const struct { const char *name; CettaRelativeModulePolicy policy; } names[] = {
        {"relative", CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
        {"current-dir", CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
        {"script-dir", CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY},
        {"ancestor-walk", CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK},
        {"walk", CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK},
        {"upstream", CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY},
        {"working-dir", CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY},
        {"cwd", CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY},
    };
    if (!name) return false;
    for (size_t i = 0; i < COUNT(names); i++)
        if (ascii_ieq(names[i].name, name)) {
            if (out_policy) *out_policy = names[i].policy;
            return true;
        }
    return false;
}

CettaRelativeModulePolicy cetta_language_relative_module_policy(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec ? spec->relative_module_policy : CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
}

CettaPortableRelationalContract
cetta_language_portable_relational_contract(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec ? spec->portable_relational_contract : CETTA_PORTABLE_RELATIONAL_NONE;
}

CettaCallResultPolicy cetta_language_call_result_policy(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec ? spec->call_result_policy : CETTA_CALL_RESULT_BY_VALUE;
}

bool cetta_language_rules_need_named_head(CettaLanguageId id) {
    const CettaLanguageSpec *spec = cetta_language_from_id(id);
    return spec && spec->rules_need_named_head;
}

/* The implemented languages, then the planned ones, each in the table's order. */
void cetta_language_print_inventory(FILE *out) {
    fputs("cetta language inventory (--lang driver/front-end):\n", out);
    for (int implemented = 1; implemented >= 0; implemented--)
        for (size_t i = 0; i < COUNT(CETTA_LANGUAGES); i++) {
            const CettaLanguageSpec *spec = &CETTA_LANGUAGES[i];
            if (spec->implemented != (bool)implemented) continue;
            fprintf(out, "  %-16s %s  %s\n", spec->canonical,
                    spec->implemented ? "implemented" : "planned", spec->note);
        }
}
