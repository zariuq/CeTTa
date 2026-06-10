#include "langdef_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── FNV-1a 64 digest over the canonical rule-descriptor text ──────────── */

#define CETTA_LANGDEF_FNV_OFFSET 14695981039346656037ULL
#define CETTA_LANGDEF_FNV_PRIME 1099511628211ULL

static uint64_t fnv1a64_field(uint64_t h, const char *s) {
    if (s) {
        while (*s) {
            h ^= (unsigned char)*s++;
            h *= CETTA_LANGDEF_FNV_PRIME;
        }
    }
    /* field separator so adjacent fields cannot alias */
    h ^= 0x1fu;
    h *= CETTA_LANGDEF_FNV_PRIME;
    return h;
}

static uint64_t fnv1a64_u32(uint64_t h, uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%u", v);
    return fnv1a64_field(h, buf);
}

static uint64_t pack_source_digest(const CettaLangdefPack *pack) {
    uint64_t h = CETTA_LANGDEF_FNV_OFFSET;
    h = fnv1a64_field(h, pack->language_id);
    h = fnv1a64_field(h, pack->profile_id);
    h = fnv1a64_field(h, pack->granularity);
    h = fnv1a64_u32(h, pack->schema_version);
    h = fnv1a64_field(h, pack->claim_level);
    for (uint32_t i = 0; i < pack->rule_count; i++) {
        const CettaLangdefRuleDef *r = &pack->rules[i];
        h = fnv1a64_field(h, r->name);
        h = fnv1a64_u32(h, (uint32_t)r->rule_id);
        h = fnv1a64_field(h, r->profiles);
        h = fnv1a64_field(h, r->provenance);
        h = fnv1a64_field(h, r->live ? "live" : "structural");
    }
    return h;
}

/* ── Disabled-rule mask from CETTA_LANGDEF_DISABLED_RULES ──────────────── */

static uint32_t pack_disabled_mask(const CettaLangdefPack *pack) {
    const char *env = getenv("CETTA_LANGDEF_DISABLED_RULES");
    uint32_t mask = 0;
    const char *p;

    if (!env || !*env)
        return 0;

    p = env;
    while (*p) {
        const char *start = p;
        size_t len;
        bool matched = false;

        while (*p && *p != ',')
            p++;
        len = (size_t)(p - start);
        if (len > 0) {
            for (uint32_t i = 0; i < pack->rule_count; i++) {
                const CettaLangdefRuleDef *r = &pack->rules[i];
                if (strlen(r->name) == len &&
                    strncmp(r->name, start, len) == 0) {
                    mask |= (1u << (uint32_t)r->rule_id);
                    matched = true;
                }
            }
            if (!matched) {
                fprintf(stderr,
                        "cetta: warning: CETTA_LANGDEF_DISABLED_RULES names "
                        "unknown rule '%.*s' (ignored)\n",
                        (int)len, start);
            }
        }
        if (*p == ',')
            p++;
    }
    return mask;
}

void cetta_langdef_pack_finalize(CettaLangdefPack *pack) {
    pack->source_digest = pack_source_digest(pack);
    pack->disabled_mask = pack_disabled_mask(pack);
}

bool cetta_langdef_pack_rule_enabled(const CettaLangdefPack *pack,
                                     CettaLangdefRuleId rule_id) {
    uint32_t i;
    if (!pack || rule_id >= CETTA_LANGDEF_RULE_COUNT)
        return false;
    if (pack->disabled_mask & (1u << (uint32_t)rule_id))
        return false;
    for (i = 0; i < pack->rule_count; i++) {
        if (pack->rules[i].rule_id == rule_id)
            return pack->rules[i].live;
    }
    return false;
}

/* ── Reflection atom ───────────────────────────────────────────────────── */

Atom *cetta_langdef_pack_info_atom(const CettaLangdefPack *pack, Arena *a) {
    char digest_buf[32];
    Atom *elems[8];
    Atom *rule_elems[CETTA_LANGDEF_RULE_COUNT + 1];
    Atom *disabled_elems[CETTA_LANGDEF_RULE_COUNT + 1];
    uint32_t rule_len = 0, disabled_len = 0, i;

    snprintf(digest_buf, sizeof digest_buf, "fnv1a64:%016llx",
             (unsigned long long)pack->source_digest);

    rule_elems[rule_len++] = atom_symbol(a, "rules");
    disabled_elems[disabled_len++] = atom_symbol(a, "disabled");
    for (i = 0; i < pack->rule_count; i++) {
        const CettaLangdefRuleDef *r = &pack->rules[i];
        rule_elems[rule_len++] = atom_symbol(a, r->name);
        if (pack->disabled_mask & (1u << (uint32_t)r->rule_id))
            disabled_elems[disabled_len++] = atom_symbol(a, r->name);
    }

    elems[0] = atom_symbol(a, "langdef-pack");
    elems[1] = atom_symbol(a, pack->language_id);
    elems[2] = atom_symbol(a, pack->profile_id);
    elems[3] = atom_symbol(a, pack->granularity);
    elems[4] = atom_string(a, digest_buf);
    elems[5] = atom_expr2(a, atom_symbol(a, "claim"),
                          atom_symbol(a, pack->claim_level));
    elems[6] = atom_expr(a, rule_elems, rule_len);
    elems[7] = atom_expr(a, disabled_elems, disabled_len);
    return atom_expr(a, elems, 8);
}
