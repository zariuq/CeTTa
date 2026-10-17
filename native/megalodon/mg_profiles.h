#ifndef CETTA_MEGALODON_PROFILES_H
#define CETTA_MEGALODON_PROFILES_H

#include "mg_core.h"

typedef struct {
    uint32_t index;
    const char *name;
    uint32_t type_variables;
    const char *type;     /* Megalodon -sexprinfo spelling */
} MgPrimSpec;

typedef struct {
    MgProfileTag tag;
    const char *name;
    const char *upstream_flag;
    const char *proofgold_theory_id;   /* recorded identity, may be NULL */
    const MgPrimSpec *prims;
    uint32_t prim_count;
    const char *const *reserved;
    uint32_t reserved_count;
} MgProfileSpec;

const MgProfileSpec *mg_profile_spec(MgProfileTag tag);
/* A front-end type with `levels` enclosing type parameters, wrapped in
 * `levels` TpAll binders (syntax.ml to_mathdata_ptp). */
MgTp *mg_profile_parse_type(MgStore *s, const char *text, uint32_t levels);

#endif /* CETTA_MEGALODON_PROFILES_H */
