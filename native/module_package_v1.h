#ifndef CETTA_MODULE_PACKAGE_V1_H
#define CETTA_MODULE_PACKAGE_V1_H
#include "module_value_v1.h"
/* File dependencies instantiate the module snapshot with SHA-256 commitments
 * to exact captured file bytes. Export references remain explicit data. */
Atom *cetta_module_package_close_v1(Arena *, Atom *);
Atom *cetta_module_package_export_v1(Arena *, Atom *, Atom *);
#endif
