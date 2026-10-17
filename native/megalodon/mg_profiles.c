#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_profiles.h"

#include <string.h>

/*
 * Upstream profile inventories.  The primitive tables are generated from
 * megalodon-upstream/src/syntax.ml (`primname`) by
 * tools/megalodon_profiles_from_upstream.py; see that file's header for the
 * exact source hash.  Profile tags authorize nothing: a theory's parameters
 * and axioms are admitted only through its explicit selected authority.
 */

#include "mg_profile_prims.inc"

static const char *const g_setmm_reserved[] = {"wi", "wal"};

/* Proofgold theory identities printed by upstream megalodon.ml (pfg output,
 * "Document <id>") and syntax.ml (hfthyid, egalthyid, ...).  Recorded as the
 * selected upstream identity; native admission does not derive them. */
static const MgProfileSpec g_profiles[] = {
    {MG_PROFILE_HF, "HF", "-hf",
     "6ffc9680fbe00a58d70cdeb319f11205ed998131ce51bb96f16c7904faf74a3d",
     mg_prims_hf, MG_PRIMS_HF_COUNT, NULL, 0u},
    {MG_PROFILE_EGAL, "Egal", "(default)",
     "29c988c5e6c620410ef4e61bcfcbe4213c77013974af40759d8b732c07d61967",
     mg_prims_egal, MG_PRIMS_EGAL_COUNT, NULL, 0u},
    {MG_PROFILE_MIZAR, "Mizar", "-mizar",
     "5ab3df7b0b4ef20889de0517a318df8746940971ad9b2021e54c820eb9e74dce",
     mg_prims_mizar, MG_PRIMS_MIZAR_COUNT, NULL, 0u},
    {MG_PROFILE_HOAS, "HOAS", "-hoas",
     "513140056e2032628f48d11e221efe29892e9a03a661d3b691793524a5176ede",
     mg_prims_hoas, MG_PRIMS_HOAS_COUNT, NULL, 0u},
    {MG_PROFILE_SETMM, "SetMM", "-setmm",
     "85ecfdcf26657b94532af5af2393c6945cee05c4aabccb8a819f793a7dbc4acf",
     mg_prims_setmm, MG_PRIMS_SETMM_COUNT, g_setmm_reserved, 2u},
    {MG_PROFILE_CUSTOM, "Custom", "(none)", NULL, NULL, 0u, NULL, 0u},
};

const MgProfileSpec *mg_profile_spec(MgProfileTag tag) {
    for (size_t i = 0; i < sizeof(g_profiles) / sizeof(g_profiles[0]); ++i)
        if (g_profiles[i].tag == tag)
            return &g_profiles[i];
    return NULL;
}

const char *mg_profile_name(MgProfileTag tag) {
    const MgProfileSpec *spec = mg_profile_spec(tag);
    return spec ? spec->name : "invalid";
}

bool mg_profile_from_name(const char *name, MgProfileTag *out) {
    if (!name)
        return false;
    for (size_t i = 0; i < sizeof(g_profiles) / sizeof(g_profiles[0]); ++i) {
        const char *a = g_profiles[i].name;
        size_t n = strlen(a);
        if (strlen(name) != n)
            continue;
        bool same = true;
        for (size_t j = 0; j < n && same; ++j) {
            char x = a[j], y = name[j];
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            same = x == y;
        }
        if (same) {
            *out = g_profiles[i].tag;
            return true;
        }
    }
    return false;
}

/* Parse Megalodon's -sexprinfo type spelling:
 *   (PROP) (SET) (TPVAR i) (AR a b)
 * SET is base type 0 (syntax.ml to_mathdata_tp).  A TPVAR i is a
 * declaration-local level; `levels` is the number of enclosing type
 * parameters, and the Mathdata index is levels - i - 1. */
static const char *skip(const char *p) {
    while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')
        ++p;
    return p;
}

static MgTp *parse_tp(MgStore *s, const char **pp, uint32_t levels) {
    const char *p = skip(*pp);
    if (*p != '(')
        return NULL;
    p = skip(p + 1);
    MgTp *r = NULL;
    if (strncmp(p, "PROP", 4) == 0) {
        p += 4;
        r = mg_tp_prop(s);
    } else if (strncmp(p, "SET", 3) == 0) {
        p += 3;
        r = mg_tp_base(s, 0);
    } else if (strncmp(p, "TPVAR", 5) == 0) {
        p = skip(p + 5);
        uint64_t n = 0;
        if (*p < '0' || *p > '9')
            return NULL;
        while (*p >= '0' && *p <= '9') {
            n = n * 10u + (uint64_t)(*p - '0');
            if (n > UINT32_MAX)
                return NULL;
            ++p;
        }
        if (n >= levels)
            return NULL;
        r = mg_tp_var(s, levels - (uint32_t)n - 1u);
    } else if (strncmp(p, "AR", 2) == 0) {
        p += 2;
        MgTp *a = parse_tp(s, &p, levels);
        MgTp *b = a ? parse_tp(s, &p, levels) : NULL;
        r = b ? mg_tp_arr(s, a, b) : NULL;
    } else {
        return NULL;
    }
    p = skip(p);
    if (!r || *p != ')')
        return NULL;
    *pp = p + 1;
    return r;
}

MgTp *mg_profile_parse_type(MgStore *s, const char *text, uint32_t levels) {
    const char *p = text;
    MgTp *r = parse_tp(s, &p, levels);
    if (!r || *skip(p) != '\0')
        return NULL;
    for (uint32_t i = 0; i < levels; ++i)
        r = mg_tp_all(s, r);
    return r;
}
