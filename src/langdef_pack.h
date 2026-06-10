#ifndef CETTA_LANGDEF_PACK_H
#define CETTA_LANGDEF_PACK_H

/* LangDef pack: a reflectable, rule-indexed description of a coarse one-step
 * reduction relation (the HEFrontier granularity), welded into the one-step
 * evaluator so that covered rule classes are reachable only through the pack.
 *
 * Slice 1 is hand-authored but generator-shaped: every field here is exactly
 * what a future --compile-langdef tool would emit (rule IDs, profile surface,
 * provenance links to the fine instruction-machine rules, source digest,
 * disabled mask, claim level).  The pack is data the runtime consults; the
 * native C implementations remain the leaf functions of each rule.
 *
 * Granularity note: rules here describe the coarse user-visible frontier
 * (one observable surface rewrite), not the fine interpreter state machine.
 * Provenance strings name the fine rules that justify each coarse rule.
 */

#include "atom.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CETTA_HEF_RULE_GROUNDED_DISPATCH = 0,
    CETTA_HEF_RULE_LEFTMOST_EXPR_CONGRUENCE,
    CETTA_HEF_RULE_QUOTE_QUIESCENT,
    CETTA_HEF_RULE_RETURN_QUIESCENT,
    CETTA_HEF_RULE_EMPTY_QUIESCENT,
    CETTA_HEF_RULE_ERROR_QUIESCENT,
    CETTA_LANGDEF_RULE_COUNT
} CettaLangdefRuleId;

typedef struct {
    const char *name;       /* stable coarse rule ID, e.g. "HEF_GroundedDispatch" */
    CettaLangdefRuleId rule_id;
    const char *profiles;   /* authored profile surface for this rule */
    const char *provenance; /* fine-machine rules justifying this coarse rule */
    bool live;              /* live rules gate an executable branch; non-live
                             * rules are structural metadata (documented
                             * quiescence behavior not yet pack-routed) */
} CettaLangdefRuleDef;

typedef struct {
    const char *language_id;  /* "HE" */
    const char *profile_id;   /* authored target profile of this pack */
    const char *granularity;  /* "frontier" */
    uint32_t schema_version;
    const char *claim_level;  /* honest conformance claim, e.g.
                               * "bag-tested-adequate" (tested against the
                               * legacy frontier and oracle bags; not yet
                               * Lean-proven) */
    const CettaLangdefRuleDef *rules;
    uint32_t rule_count;
    uint64_t source_digest;   /* FNV-1a 64 over the canonical rule-descriptor
                               * text; sha256 arrives with --compile-langdef */
    uint32_t disabled_mask;   /* bit per rule_id; populated from the
                               * CETTA_LANGDEF_DISABLED_RULES env var
                               * (comma-separated rule names) at first use */
} CettaLangdefPack;

/* The HE frontier pack singleton (lazily initialized; reads
 * CETTA_LANGDEF_DISABLED_RULES once on first use). */
const CettaLangdefPack *cetta_langdef_pack_he_frontier(void);

/* True when the rule is live and not disabled.  Covered evaluator branches
 * must be reachable only through this check, so that disabling a rule cannot
 * be compensated by a legacy code path. */
bool cetta_langdef_pack_rule_enabled(const CettaLangdefPack *pack,
                                     CettaLangdefRuleId rule_id);

/* Reflection atom:
 * (langdef-pack HE he-extended frontier "fnv1a64:<hex>" (schema N)
 *   (claim <level>) (rules <name>...) (disabled <name>...)) */
Atom *cetta_langdef_pack_info_atom(const CettaLangdefPack *pack, Arena *a);

/* Shared finalizer used by pack instances: computes the source digest over
 * the canonical descriptor text and parses the disabled mask from the
 * environment. */
void cetta_langdef_pack_finalize(CettaLangdefPack *pack);

#endif /* CETTA_LANGDEF_PACK_H */
