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
 * Granularity note: rules here describe the coarse user-visible one-step relation
 * (one observable surface rewrite), not the fine interpreter state machine.
 * Provenance strings name the fine rules that justify each coarse rule.
 */

#include "atom.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CETTA_HES_RULE_GROUNDED_DISPATCH = 0,
    CETTA_HES_RULE_LEFTMOST_EXPR_CONGRUENCE,
    CETTA_HES_RULE_EQUATION_MATCH,
    CETTA_HES_RULE_EVAL,
    CETTA_HES_RULE_CHAIN,
    CETTA_HES_RULE_LET,
    CETTA_HES_RULE_LET_STAR,
    CETTA_HES_RULE_CASE,
    CETTA_HES_RULE_SWITCH,
    CETTA_HES_RULE_SWITCH_MINIMAL,
    CETTA_HES_RULE_SPACE_MEET,
    CETTA_HES_RULE_SPACE_ADDITIVE_UNION,
    CETTA_HES_RULE_SOURCED_CONJUNCTION_MATCH,
    CETTA_HES_RULE_QUOTE_QUIESCENT,
    CETTA_HES_RULE_RETURN_QUIESCENT,
    CETTA_HES_RULE_EMPTY_QUIESCENT,
    CETTA_HES_RULE_ERROR_QUIESCENT,
    CETTA_LANGDEF_RULE_COUNT
} CettaLangdefRuleId;

typedef struct {
    const char *name;       /* stable coarse rule ID, e.g. "HES_GroundedDispatch" */
    CettaLangdefRuleId rule_id;
    const char *profiles;   /* authored profile surface for this rule */
    const char *provenance; /* fine-machine rules justifying this coarse rule */
    const char *claim;      /* per-rule epistemic level: "bag-tested-adequate"
                             * (tested against legacy path and oracle bags),
                             * "rule-sound-on-fragment" (a Lean soundness
                             * theorem exists on a clearly characterized
                             * fragment, named in the rule's comment), or
                             * "rule-sound" (an unconditional Lean soundness
                             * theorem exists).
                             * Evidence about the rule, not rule content —
                             * deliberately excluded from the source digest. */
    bool live;              /* live rules gate an executable branch; non-live
                             * rules are structural metadata (documented
                             * quiescence behavior not yet pack-routed) */
} CettaLangdefRuleDef;

typedef struct {
    const char *language_id;  /* "HE" */
    const char *profile_id;   /* authored target profile of this pack */
    const char *granularity;  /* "small-step" */
    uint32_t schema_version;
    const CettaLangdefRuleDef *rules;
    uint32_t rule_count;
    uint64_t source_digest;   /* FNV-1a 64 over the canonical rule-descriptor
                               * text; sha256 arrives with --compile-langdef */
    uint32_t disabled_mask;   /* bit per rule_id; populated from the
                               * CETTA_LANGDEF_DISABLED_RULES env var
                               * (comma-separated rule names) at first use */
} CettaLangdefPack;

/* The HE small-step rule table singleton (lazily initialized; reads
 * CETTA_LANGDEF_DISABLED_RULES once on first use). */
const CettaLangdefPack *cetta_langdef_pack_he_small_step(void);

/* True when the rule is live and not disabled.  Covered evaluator branches
 * must be reachable only through this check, so that disabling a rule cannot
 * be compensated by a legacy code path. */
bool cetta_langdef_pack_rule_enabled(const CettaLangdefPack *pack,
                                     CettaLangdefRuleId rule_id);

/* Reflection atom (the rule table as data; deliberately carries no
 * active-profile claim — per-profile tables are a later milestone):
 * (step-rules HE small-step "fnv1a64:<hex>" (schema N)
 *   (rules (<name> <claim>)...) (disabled <name>...)) */
Atom *cetta_langdef_step_rules_atom(const CettaLangdefPack *pack, Arena *a);

/* Shared finalizer used by pack instances: computes the source digest over
 * the canonical descriptor text and parses the disabled mask from the
 * environment. */
void cetta_langdef_pack_finalize(CettaLangdefPack *pack);

#endif /* CETTA_LANGDEF_PACK_H */
