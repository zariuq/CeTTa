#ifndef CETTA_MODULE_FIELDS_V1_H
#define CETTA_MODULE_FIELDS_V1_H

#include "module_value_v1.h"
#include "language_def_core_v1.h"

/* Raw declaration documents have a law-free, module-authored signature.
 * Executable elaboration is a separate consumer, not an attachment effect. */
Atom *cetta_module_attach_layer_v1(Arena *, Atom *base, Atom *key,
    Atom *schema, const CettaLanguageDefCoreV1 *, Atom *category, Atom *fields);
Atom *cetta_module_join_layers_v1(Arena *, Atom *left, Atom *right);
/* Explicitly change the base of core-independent raw rows. The caller
 * supplies the assembled target; this is not an implicit core union. */
Atom *cetta_module_join_layers_over_v1(Arena *, Atom *target, Atom *left, Atom *right);
Atom *cetta_module_base_core_v1(Arena *, Atom *value);
Atom *cetta_module_layer_fields_v1(Arena *, Atom *value, Atom *key);

#endif
