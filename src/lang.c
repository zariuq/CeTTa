#include "lang.h"
#include "atom.h"
#include <ctype.h>
#include <string.h>

static const char *CETTA_PETTA_PRELUDE =
    "(: msort (-> Atom Expression))\n"
    "(: sort-atom (-> Atom Expression))\n";

static const CettaTypeDeclSignature CETTA_PETTA_STDLIB_TYPE_PRUNES[] = {
    {"car-atom", "Expression", "Atom"},
    {"cdr-atom", "Expression", "Expression"},
    {"unique-atom", "Expression", "Expression"},
    {"alpha-unique-atom", "Expression", "Expression"},
};

static const CettaLanguageSpec CETTA_LANGUAGES[] = {
    {CETTA_LANGUAGE_HE, "he", "he", true, "Current direct MeTTa/HE evaluator",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "mettahe", "he", true, "Current direct MeTTa/HE evaluator",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_MM2, "mm2", "mm2", true,
     "Direct MM2 lowering plus MORK-backed execution for pure program files",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_PETTA, "petta", "petta", true,
     "PeTTa surface adapter over the shared C kernel",
     {.known_head_query_miss_is_failure = true,
      .unique_atom_alpha_equivalence_for_open_terms = false,
      .inject_builtin_type_decls = false,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "ambient", "ambient", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "calculator", "calculator", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "imp", "imp", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "lambda", "lambda", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "mettafull-legacy", "mettafull-legacy", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "minskylite", "minskylite", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "mm0lite", "mm0lite", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "pyashcore", "pyashcore", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "rhocalc", "rhocalc", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
};

static bool head_matches(SymbolId head_id, const char *name) {
    return g_symbols && symbol_eq_cstr(g_symbols, head_id, name);
}

static bool petta_slot_policy_override(SymbolId head_id, uint32_t arg_index,
                                       CettaFunctionArgPolicy *out_policy) {
    if (head_matches(head_id, "if") && (arg_index == 1 || arg_index == 2)) {
        *out_policy = CETTA_FUNCTION_ARG_POLICY_RAW;
        return true;
    }
    if (head_id == g_builtin_syms.superpose && arg_index == 0) {
        *out_policy = CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL;
        return true;
    }
    return false;
}

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

bool cetta_language_known_head_query_miss_is_failure(const CettaLanguageSpec *spec) {
    return spec && spec->semantics.known_head_query_miss_is_failure;
}

bool cetta_language_unique_atom_uses_alpha_for_open_terms(const CettaLanguageSpec *spec) {
    return !spec || spec->semantics.unique_atom_alpha_equivalence_for_open_terms;
}

bool cetta_language_injects_builtin_type_decls(const CettaLanguageSpec *spec) {
    return !spec || spec->semantics.inject_builtin_type_decls;
}

CettaFunctionArgPolicy cetta_language_function_arg_policy(const CettaLanguageSpec *spec,
                                                          SymbolId head_id,
                                                          uint32_t arg_index,
                                                          Atom *domain_type) {
    if (!spec)
        return CETTA_FUNCTION_ARG_POLICY_RAW;
    if (spec->id == CETTA_LANGUAGE_PETTA) {
        CettaFunctionArgPolicy override = CETTA_FUNCTION_ARG_POLICY_RAW;
        if (petta_slot_policy_override(head_id, arg_index, &override))
            return override;
    }
    if (!domain_type || atom_is_symbol_id(domain_type, g_builtin_syms.undefined_type))
        return spec->semantics.undefined_arg_policy;
    if (atom_is_symbol_id(domain_type, g_builtin_syms.atom))
        return spec->semantics.atom_arg_policy;
    if (atom_is_symbol_id(domain_type, g_builtin_syms.expression))
        return spec->semantics.expression_arg_policy;
    return CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL;
}

const CettaTypeDeclSignature *cetta_language_stdlib_type_prunes(const CettaLanguageSpec *spec,
                                                                size_t *count_out) {
    if (count_out)
        *count_out = 0;
    if (!spec || spec->id != CETTA_LANGUAGE_PETTA)
        return NULL;
    if (count_out) {
        *count_out =
            sizeof(CETTA_PETTA_STDLIB_TYPE_PRUNES) /
            sizeof(CETTA_PETTA_STDLIB_TYPE_PRUNES[0]);
    }
    return CETTA_PETTA_STDLIB_TYPE_PRUNES;
}

const char *cetta_language_prelude_text(const CettaLanguageSpec *spec) {
    if (!spec || !spec->canonical)
        return NULL;
    if (strcmp(spec->canonical, "petta") == 0)
        return CETTA_PETTA_PRELUDE;
    return NULL;
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
