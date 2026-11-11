#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_upstream.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "mg_upstream_tables.inc"

#define ROWS(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

typedef struct {
    const MgUpstreamPrim *rows;
    uint32_t count;
    const MgUpstreamPrim **sorted;
} PrimTable;

static PrimTable g_tables[MG_PROFILE_COUNT] = {
    [MG_PROFILE_HF] = {mg_pfghfprim, ROWS(mg_pfghfprim), NULL},
    [MG_PROFILE_EGAL] = {mg_pfgegalprim, ROWS(mg_pfgegalprim), NULL},
    [MG_PROFILE_MIZAR] = {mg_pfgmizprim, ROWS(mg_pfgmizprim), NULL},
    [MG_PROFILE_HOAS] = {mg_pfghoasprim, ROWS(mg_pfghoasprim), NULL},
    [MG_PROFILE_SETMM] = {mg_pfgsetmmprim, ROWS(mg_pfgsetmmprim), NULL},
};
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_ready;

static int by_hash(const void *a, const void *b) {
    return strcmp((*(const MgUpstreamPrim *const *)a)->hash, (*(const MgUpstreamPrim *const *)b)->hash);
}

/* A hash added twice is found at its last addition (Hashtbl.find sees the
 * newest binding): sort stably by hash and keep the last of equal runs. */
static void build(void) {
    for (int t = 0; t < MG_PROFILE_COUNT; ++t) {
        PrimTable *p = &g_tables[t];
        if (!p->rows)
            continue;
        const MgUpstreamPrim **s = malloc(sizeof(*s) * p->count);
        if (!s)
            return;
        for (uint32_t i = 0; i < p->count; ++i)
            s[i] = &p->rows[i];
        /* insertion order breaks ties: pointers into one array compare by
         * position, so a stable order follows from sorting (hash, address) */
        for (uint32_t i = 1; i < p->count; ++i)
            for (uint32_t j = i; j > 0; --j) {
                int c = strcmp(s[j - 1]->hash, s[j]->hash);
                if (c < 0 || (c == 0 && s[j - 1] < s[j]))
                    break;
                const MgUpstreamPrim *x = s[j];
                s[j] = s[j - 1];
                s[j - 1] = x;
            }
        p->sorted = s;
    }
    g_ready = true;
}

bool mg_upstream_pfg_prim(MgProfileTag theory, const char *hex, uint32_t *index) {
    if ((int)theory < 0 || theory >= MG_PROFILE_COUNT)
        return false;
    pthread_once(&g_once, build);
    const PrimTable *p = &g_tables[theory];
    if (!g_ready || !p->sorted)
        return false;
    MgUpstreamPrim key = {hex, 0};
    const MgUpstreamPrim *k = &key;
    const MgUpstreamPrim **hit = bsearch(&k, p->sorted, p->count, sizeof(*p->sorted), by_hash);
    if (!hit)
        return false;
    /* the newest of equal hashes */
    while ((uint32_t)(hit - p->sorted) + 1u < p->count && strcmp(hit[1]->hash, hex) == 0)
        ++hit;
    *index = (*hit)->index;
    return true;
}

static MgUpstreamPreset preset(const MgUpstreamIndexed *tms, uint32_t tm_count,
                               const char *const *knowns, uint32_t known_count, bool clears) {
    return (MgUpstreamPreset){tms, tm_count, knowns, known_count, clears};
}

uint32_t mg_upstream_presets(MgProfileTag theory, MgUpstreamPreset out[2]) {
    uint32_t n = 0;
    out[n++] = preset(mg_preset_mizar_index_tms, MG_PRESET_MIZAR_INDEX_TMS,
                      mg_preset_mizar_index_knowns, MG_PRESET_MIZAR_INDEX_KNOWNS,
                      MG_PRESET_MIZAR_INDEX_CLEARS);
    switch (theory) {
    case MG_PROFILE_HF:
        out[n++] = preset(mg_preset_hf_index_tms, MG_PRESET_HF_INDEX_TMS,
                          mg_preset_hf_index_knowns, MG_PRESET_HF_INDEX_KNOWNS,
                          MG_PRESET_HF_INDEX_CLEARS);
        break;
    case MG_PROFILE_MIZAR:
        out[n++] = preset(mg_preset_mizar_index_tms, MG_PRESET_MIZAR_INDEX_TMS,
                          mg_preset_mizar_index_knowns, MG_PRESET_MIZAR_INDEX_KNOWNS,
                          MG_PRESET_MIZAR_INDEX_CLEARS);
        break;
    case MG_PROFILE_HOAS:
        out[n++] = preset(mg_preset_hoas_index_tms, MG_PRESET_HOAS_INDEX_TMS,
                          mg_preset_hoas_index_knowns, MG_PRESET_HOAS_INDEX_KNOWNS,
                          MG_PRESET_HOAS_INDEX_CLEARS);
        break;
    case MG_PROFILE_SETMM:
        out[n++] = preset(mg_preset_setmm_index_tms, MG_PRESET_SETMM_INDEX_TMS,
                          mg_preset_setmm_index_knowns, MG_PRESET_SETMM_INDEX_KNOWNS,
                          MG_PRESET_SETMM_INDEX_CLEARS);
        break;
    default:
        break;
    }
    return n;
}

const char *mg_upstream_distinct(const char *name) {
    for (uint32_t i = 0; i < ROWS(mg_distinct_names); ++i)
        if (strcmp(mg_distinct_names[i].name, name) == 0)
            return mg_distinct_names[i].hash;
    return NULL;
}
