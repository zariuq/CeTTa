#ifndef CETTA_MODULE_VALUE_INTERNAL_V1_H
#define CETTA_MODULE_VALUE_INTERNAL_V1_H

#include "module_value_v1.h"

#define MODULE_TUPLE_CATEGORY_PREFIX_V1 "$module:tuple:"

typedef struct { Atom **items; size_t len, capacity; } ModuleValuesV1;
typedef struct {
    Arena *arena;
    uint32_t remaining;
    uint32_t depth;
    Atom *site, *refusal, *source;
    ModuleValuesV1 sites;
} ModuleWorkV1;

bool module_form_v1(const Atom *, const char *, size_t);
bool module_string_v1(const Atom *);
Atom *module_make_v1(Arena *, const char *, Atom *const *, size_t);
bool module_fail_v1(ModuleWorkV1 *, const char *, const char *);
bool module_spend_v1(ModuleWorkV1 *);
bool module_push_v1(ModuleWorkV1 *, ModuleValuesV1 *, Atom *);
void module_clear_v1(ModuleValuesV1 *);
Atom *module_list_v1(Arena *, const ModuleValuesV1 *);
bool module_unlist_v1(ModuleWorkV1 *, Atom *, ModuleValuesV1 *);
Atom *module_text_v1(ModuleWorkV1 *, Atom *);
Atom *module_category_name_v1(ModuleWorkV1 *, Atom *);
Atom *module_site_v1(ModuleWorkV1 *, Atom *, Atom *);
Atom *module_core_v1(ModuleWorkV1 *, Atom *, Atom *);
Atom *module_interpret_v1(ModuleWorkV1 *, Atom *, Atom *, Atom *);

#endif
