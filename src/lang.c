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
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "mettahe", "he", true, "Current direct MeTTa/HE evaluator",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_MM2, "mm2", "mm2", true,
     "Direct MM2 lowering plus MORK-backed execution for pure program files",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_PETTA, "petta", "petta", true,
     "PeTTa surface adapter over the shared C kernel",
     {.known_head_query_miss_is_failure = true,
      .unique_atom_alpha_equivalence_for_open_terms = false,
      .inject_builtin_type_decls = false,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_AUTO_CREATE,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_SCRIPT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW}},
    {CETTA_LANGUAGE_HE, "ambient", "ambient", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "calculator", "calculator", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "imp", "imp", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "lambda", "lambda", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "mettafull-legacy", "mettafull-legacy", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "minskylite", "minskylite", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "mm0lite", "mm0lite", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "pyashcore", "pyashcore", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
    {CETTA_LANGUAGE_HE, "rhocalc", "rhocalc", false, "Planned port from mettail-rust inventory",
     {.known_head_query_miss_is_failure = false,
      .unique_atom_alpha_equivalence_for_open_terms = true,
      .inject_builtin_type_decls = true,
      .named_space_policy = CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY,
      .relative_module_policy = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY,
      .undefined_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL,
      .atom_arg_policy = CETTA_FUNCTION_ARG_POLICY_RAW,
      .expression_arg_policy = CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL}},
};

static _Thread_local const CettaLanguageSpec *g_cetta_current_language = NULL;

static bool head_matches(SymbolId head_id, const char *name) {
    return g_symbols && symbol_eq_cstr(g_symbols, head_id, name);
}

static bool language_slot_policy_override(const CettaLanguageSpec *spec,
                                          SymbolId head_id,
                                          uint32_t arg_index,
                                          CettaFunctionArgPolicy *out_policy) {
    if (head_id == g_builtin_syms.pl_consult ||
        head_id == g_builtin_syms.pl_use_module ||
        head_id == g_builtin_syms.pl_atom ||
        head_id == g_builtin_syms.pl_import ||
        head_id == g_builtin_syms.import_prolog_function ||
        head_id == g_builtin_syms.predicate_ctor ||
        head_id == g_builtin_syms.pl_call ||
        head_id == g_builtin_syms.callPredicate ||
        head_id == g_builtin_syms.assertzPredicate ||
        head_id == g_builtin_syms.assertaPredicate ||
        head_id == g_builtin_syms.retractPredicate) {
        (void)arg_index;
        *out_policy = CETTA_FUNCTION_ARG_POLICY_RAW;
        return true;
    }
    if (head_matches(head_id, "if") && (arg_index == 1 || arg_index == 2)) {
        *out_policy = CETTA_FUNCTION_ARG_POLICY_RAW;
        return true;
    }
    if (head_id == g_builtin_syms.superpose && arg_index == 0) {
        *out_policy = (spec && spec->id == CETTA_LANGUAGE_PETTA)
                          ? CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL
                          : CETTA_FUNCTION_ARG_POLICY_RAW;
        return true;
    }
    if ((head_id == g_builtin_syms.cons_atom && arg_index < 2) ||
        (head_id == g_builtin_syms.union_atom && arg_index < 2) ||
        (head_id == g_builtin_syms.decons_atom && arg_index == 0) ||
        (head_id == g_builtin_syms.car_atom && arg_index == 0) ||
        (head_id == g_builtin_syms.cdr_atom && arg_index == 0)) {
        *out_policy = (spec && spec->id == CETTA_LANGUAGE_PETTA)
                          ? CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL
                          : CETTA_FUNCTION_ARG_POLICY_RAW;
        return true;
    }
    if ((head_id == g_builtin_syms.foldl_atom ||
         head_id == g_builtin_syms.minimal_foldl_atom ||
         head_id == g_builtin_syms.foldl_atom_in_space) && arg_index == 4) {
        *out_policy = CETTA_FUNCTION_ARG_POLICY_RAW;
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

const CettaLanguageSpec *cetta_language_current(void) {
    return g_cetta_current_language ? g_cetta_current_language
                                    : cetta_language_lookup("he");
}

void cetta_language_set_current(const CettaLanguageSpec *spec) {
    g_cetta_current_language = spec ? spec : cetta_language_lookup("he");
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

CettaNamedSpacePolicy cetta_language_named_space_policy(const CettaLanguageSpec *spec) {
    if (!spec)
        return CETTA_NAMED_SPACE_POLICY_EXISTING_ONLY;
    return spec->semantics.named_space_policy;
}

CettaRelativeModulePolicy cetta_language_relative_module_policy(const CettaLanguageSpec *spec) {
    if (!spec)
        return CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
    return spec->semantics.relative_module_policy;
}

CettaFunctionArgPolicy cetta_language_function_arg_policy(const CettaLanguageSpec *spec,
                                                          SymbolId head_id,
                                                          uint32_t arg_index,
                                                          Atom *domain_type) {
    if (!spec)
        return CETTA_FUNCTION_ARG_POLICY_RAW;
    CettaFunctionArgPolicy override = CETTA_FUNCTION_ARG_POLICY_RAW;
    if (language_slot_policy_override(spec, head_id, arg_index, &override))
        return override;
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

const char *cetta_language_symbol_display_name(const CettaLanguageSpec *spec,
                                               SymbolId id) {
    if (!spec)
        return NULL;
    if (spec->id == CETTA_LANGUAGE_PETTA &&
        id == g_builtin_syms.petta_empty_literal) {
        return "Empty";
    }
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
