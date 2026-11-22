#ifndef CETTA_LANG_H
#define CETTA_LANG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* CettaLanguageId and CettaSyntaxId, and every property of a language, are
 * declared once, as rows of lib/profiles.data. */
#include "generated/cetta_profiles.generated.h"

typedef enum {
    CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY = 0,
    CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK = 1,
    CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY = 2,
} CettaRelativeModulePolicy;

/* A language may expose this evaluator contract without donating any of its
 * builtins, effects, observations, or data representation to the common
 * relational machine.  Admission still requires a per-root structural proof. */
typedef enum {
    CETTA_PORTABLE_RELATIONAL_NONE = 0,
    CETTA_PORTABLE_RELATIONAL_EAGER_STRUCTURAL_EQUATIONS_V1,
} CettaPortableRelationalContract;

/* A call's result protocol is independent of argument readiness and of the
 * provenance on a completed return. Destination passing retains the caller's
 * logical receiver while the producer runs. */
typedef enum {
    CETTA_CALL_RESULT_BY_VALUE = 0,
    CETTA_CALL_RESULT_BY_DESTINATION,
} CettaCallResultPolicy;

typedef struct {
    CettaLanguageId id;
    const char *canonical;   /* its name; an alias finds the same spec */
    bool implemented;
    const char *note;
    CettaSyntaxId syntax;    /* the syntax a source is read in by default */
    CettaRelativeModulePolicy relative_module_policy;
    CettaPortableRelationalContract portable_relational_contract;
    bool embedded_gslt;
    CettaCallResultPolicy call_result_policy;
    bool rules_need_named_head;
} CettaLanguageSpec;

const CettaLanguageSpec *cetta_language_lookup(const char *name);
const CettaLanguageSpec *cetta_language_from_id(CettaLanguageId id);
const char *cetta_language_canonical_name(CettaLanguageId id);
const char *cetta_syntax_name(CettaSyntaxId syntax);
bool cetta_syntax_from_name(const char *name, CettaSyntaxId *out_syntax);
CettaSyntaxId cetta_syntax_default_for_language(CettaLanguageId id);
CettaSyntaxId cetta_syntax_infer_for_path(CettaLanguageId id, const char *path);
bool cetta_language_supports_syntax(CettaLanguageId id, CettaSyntaxId syntax);
bool cetta_language_uses_embedded_gslt(CettaLanguageId id);
const char *cetta_relative_module_policy_name(CettaRelativeModulePolicy policy);
bool cetta_relative_module_policy_from_name(const char *name,
                                            CettaRelativeModulePolicy *out_policy);
CettaRelativeModulePolicy cetta_language_relative_module_policy(CettaLanguageId id);
CettaPortableRelationalContract
cetta_language_portable_relational_contract(CettaLanguageId id);
CettaCallResultPolicy cetta_language_call_result_policy(CettaLanguageId id);
/* Whether an equation is a rule only when its left side applies a symbol.
 * PeTTa's reader makes a function only of `(= (F args...) body)` with F a
 * symbol; any other equation is a fact of its space.  HE reads a rule of any
 * head. */
bool cetta_language_rules_need_named_head(CettaLanguageId id);
void cetta_language_print_inventory(FILE *out);

#endif /* CETTA_LANG_H */
