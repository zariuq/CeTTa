#ifndef CETTA_MEGALODON_UPSTREAM_H
#define CETTA_MEGALODON_UPSTREAM_H

/*
 * Megalodon's built-in tables (generated from its sources by
 * tools/megalodon_upstream_tables.py into mg_upstream_tables.inc).
 */

#include "mg_core.h"

typedef struct {
    const char *hash;
    uint32_t index;
} MgUpstreamPrim;

typedef struct {
    const char *hash;
    const char *type;   /* Megalodon -sexprinfo spelling */
} MgUpstreamIndexed;

typedef struct {
    const char *name;
    const char *hash;
} MgUpstreamNamed;

typedef struct {
    const MgUpstreamIndexed *tms;
    uint32_t tm_count;
    const char *const *knowns;
    uint32_t known_count;
    bool clears;
} MgUpstreamPreset;

/* The Proofgold primitive that a theory gives a Megalodon identity
 * (syntax.ml pfghfprim, pfgegalprim, ...). */
bool mg_upstream_pfg_prim(MgProfileTag theory, const char *hex, uint32_t *index);

/* The preset indexes Megalodon installs for a theory, in the order it
 * applies them (megalodon.ml: preset_mizar_index always, then the option's
 * own); returns how many were written to `out`. */
uint32_t mg_upstream_presets(MgProfileTag theory, MgUpstreamPreset out[2]);

/* A named constant of the distinct tactic (e.g. "neq_3_1"), or NULL. */
const char *mg_upstream_distinct(const char *name);

#endif
