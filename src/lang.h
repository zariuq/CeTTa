#ifndef CETTA_LANG_H
#define CETTA_LANG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    CETTA_LANGUAGE_HE = 0,
    CETTA_LANGUAGE_MM2 = 1,
    CETTA_LANGUAGE_PETTA = 2,
    CETTA_LANGUAGE_AMBIENT = 3,
    CETTA_LANGUAGE_CALCULATOR = 4,
    CETTA_LANGUAGE_IMP = 5,
    CETTA_LANGUAGE_LAMBDA = 6,
    CETTA_LANGUAGE_METTAFULL_LEGACY = 7,
    CETTA_LANGUAGE_MINSKYLITE = 8,
    CETTA_LANGUAGE_MM0LITE = 9,
    CETTA_LANGUAGE_PYASHCORE = 10,
    CETTA_LANGUAGE_RHOCALC = 11
} CettaLanguageId;

typedef enum {
    CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY = 0,
    CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK = 1,
    CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY = 2,
} CettaRelativeModulePolicy;

typedef struct {
    CettaLanguageId id;
    const char *name;
    const char *canonical;
    bool implemented;
    const char *note;
    CettaRelativeModulePolicy relative_module_policy;
} CettaLanguageSpec;

const CettaLanguageSpec *cetta_language_lookup(const char *name);
const CettaLanguageSpec *cetta_language_from_id(CettaLanguageId id);
const char *cetta_language_canonical_name(CettaLanguageId id);
const char *cetta_relative_module_policy_name(CettaRelativeModulePolicy policy);
bool cetta_relative_module_policy_from_name(const char *name,
                                            CettaRelativeModulePolicy *out_policy);
CettaRelativeModulePolicy cetta_language_relative_module_policy(CettaLanguageId id);
void cetta_language_print_inventory(FILE *out);

#endif /* CETTA_LANG_H */
