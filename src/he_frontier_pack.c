#include "langdef_pack.h"

/* HEFrontier pack — the hand-authored, generator-shaped coarse one-step rule
 * table for the HE surface frontier.
 *
 * Live rules gate executable branches in metta_eval_one_step; structural
 * rules document quiescence/pass-through behavior that is accounted here but
 * not yet pack-routed (they join the live set in the S2 widening tranche).
 *
 * Provenance strings name the fine instruction-machine rules (the Lean
 * mettaHE LanguageDef) that justify each coarse rule.  The coarse/fine
 * relationship is a collapse/simulation, not a label identity.
 */

static const CettaLangdefRuleDef he_frontier_rules[] = {
    {
        .name = "HEF_GroundedDispatch",
        .rule_id = CETTA_HEF_RULE_GROUNDED_DISPATCH,
        .profiles = "he he-extended",
        .provenance = "M_Expression IE_FuncType IF_* MC_Grounded",
        .live = true,
    },
    {
        .name = "HEF_LeftmostExprCongruence",
        .rule_id = CETTA_HEF_RULE_LEFTMOST_EXPR_CONGRUENCE,
        .profiles = "he he-extended",
        .provenance = "IA_Start_* M_Expression IE_* IF_*",
        .live = true,
    },
    {
        .name = "HEF_QuoteQuiescent",
        .rule_id = CETTA_HEF_RULE_QUOTE_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "M_SymbolOrGrounded (quote surface)",
        .live = false,
    },
    {
        .name = "HEF_ReturnQuiescent",
        .rule_id = CETTA_HEF_RULE_RETURN_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "IF_AfterArgs_Call return surface",
        .live = false,
    },
    {
        .name = "HEF_EmptyQuiescent",
        .rule_id = CETTA_HEF_RULE_EMPTY_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "M_Empty",
        .live = false,
    },
    {
        .name = "HEF_ErrorQuiescent",
        .rule_id = CETTA_HEF_RULE_ERROR_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "M_Error",
        .live = false,
    },
};

const CettaLangdefPack *cetta_langdef_pack_he_frontier(void) {
    static CettaLangdefPack pack;
    static bool initialized = false;

    if (!initialized) {
        pack.language_id = "HE";
        pack.profile_id = "he-extended";
        pack.granularity = "frontier";
        pack.schema_version = 1;
        pack.claim_level = "bag-tested-adequate";
        pack.rules = he_frontier_rules;
        pack.rule_count =
            (uint32_t)(sizeof he_frontier_rules / sizeof he_frontier_rules[0]);
        cetta_langdef_pack_finalize(&pack);
        initialized = true;
    }
    return &pack;
}
