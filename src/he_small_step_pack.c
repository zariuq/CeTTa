#include "langdef_pack.h"

/* HE small-step rule table — the hand-authored, generator-shaped coarse
 * one-step rule table for the user-visible HE step relation.
 *
 * Live rules gate executable branches in metta_eval_one_step; structural
 * rules document quiescence/pass-through behavior that is accounted here but
 * not gateable (disabling them would make the semantics incoherent, e.g.
 * stepping inside quote).  All ten live rule classes (including the
 * special forms, with switch and switch-minimal split per their distinct
 * upstream contracts) are table-routed and removal-tested.
 *
 * Provenance strings name the fine instruction-machine rules (the Lean
 * mettaHE LanguageDef) that justify each coarse rule.  The coarse/fine
 * relationship is a collapse/simulation, not a label identity.
 */

static const CettaLangdefRuleDef he_small_step_rules[] = {
    {
        /* rule-sound: Lean theorem mettaCall_absorbs_grounded_dispatch
         * (Mettapedia/Languages/MeTTa/HE/SmallStepSound.lean) — the coarse
         * step is absorbed by the official declarative MettaCall. */
        .name = "HES_GroundedDispatch",
        .rule_id = CETTA_HES_RULE_GROUNDED_DISPATCH,
        .profiles = "he he-extended",
        .provenance = "M_Expression IE_FuncType IF_* MC_Grounded",
        .claim = "rule-sound",
        .live = true,
    },
    {
        /* rule-sound-on-fragment: Lean theorem
         * evalAtom_absorbs_congruence_frag
         * (Mettapedia/Languages/MeTTa/HE/SmallStepMaster.lean) — coarse
         * congruence absorbed by official EvalAtom on the certified
         * fragment (untyped constructor-like head, Q-domain spectators,
         * inner step in FragStep). */
        .name = "HES_LeftmostExprCongruence",
        .rule_id = CETTA_HES_RULE_LEFTMOST_EXPR_CONGRUENCE,
        .profiles = "he he-extended",
        .provenance = "IA_Start_* M_Expression IE_* IF_*",
        .claim = "rule-sound-on-fragment",
        .live = true,
    },
    {
        /* rule-sound: Lean theorem mettaCall_absorbs_equation_match
         * (Mettapedia/Languages/MeTTa/HE/SmallStepSound.lean). */
        .name = "HES_EquationMatch",
        .rule_id = CETTA_HES_RULE_EQUATION_MATCH,
        .profiles = "he he-extended",
        .provenance = "MC_Equation",
        .claim = "rule-sound",
        .live = true,
    },
    {
        /* rule-sound: Lean theorem minimalStep_absorbs_eval
         * (Mettapedia/Languages/MeTTa/HE/SmallStepSound.lean) — the rule's
         * content is exactly the official MinimalStep.eval instruction. */
        .name = "HES_Eval",
        .rule_id = CETTA_HES_RULE_EVAL,
        .profiles = "he he-extended",
        .provenance = "minimal.eval",
        .claim = "rule-sound",
        .live = true,
    },
    {
        /* rule-sound-on-fragment: Lean theorems
         * minimalStep_absorbs_chain_subst (substitution case, Q-domain
         * source; SmallStepQuiescence.lean) +
         * minimalStep_absorbs_chain_source (source-progress case,
         * FragStep source; SmallStepMaster.lean) against the official
         * MinimalStep.chain instruction. */
        .name = "HES_Chain",
        .rule_id = CETTA_HES_RULE_CHAIN,
        .profiles = "he he-extended",
        .provenance = "minimal.chain (incl. arity error)",
        .claim = "rule-sound-on-fragment",
        .live = true,
    },
    {
        .name = "HES_Let",
        .rule_id = CETTA_HES_RULE_LET,
        .profiles = "he he-extended",
        .provenance = "minimal.let (chain/unify sugar)",
        .claim = "bag-tested-adequate",
        .live = true,
    },
    {
        .name = "HES_LetStar",
        .rule_id = CETTA_HES_RULE_LET_STAR,
        .profiles = "he he-extended",
        .provenance = "minimal.let* (nested let sugar)",
        .claim = "bag-tested-adequate",
        .live = true,
    },
    {
        .name = "HES_Case",
        .rule_id = CETTA_HES_RULE_CASE,
        .profiles = "he he-extended",
        .provenance = "minimal.case (incl. arity error)",
        .claim = "bag-tested-adequate",
        .live = true,
    },
    {
        /* Upstream contract ((: switch (-> %Undefined% Expression
         * %Undefined%))): the scrutinee is evaluated, then matched
         * structurally.  Split from HES_SwitchMinimal 2026-06-10 after the
         * certification effort found CeTTa conflating the two spellings
         * (oracle probe: upstream three vs CeTTa (got 1 2)). */
        .name = "HES_Switch",
        .rule_id = CETTA_HES_RULE_SWITCH,
        .profiles = "he he-extended",
        .provenance = "stdlib.switch (evaluated scrutinee; incl. arity error)",
        .claim = "bag-tested-adequate",
        .live = true,
    },
    {
        /* switch-minimal: structural match on the raw scrutinee
         * ((: switch-minimal (-> Atom Expression Atom))). */
        .name = "HES_SwitchMinimal",
        .rule_id = CETTA_HES_RULE_SWITCH_MINIMAL,
        .profiles = "he he-extended",
        .provenance = "stdlib.switch-minimal (structural; incl. arity error)",
        .claim = "bag-tested-adequate",
        .live = true,
    },
    {
        /* Extended full-evaluator metadata: comma in a space position folds
         * the pointwise bag meet.  It is not a coarse one-step gate. */
        .name = "HES_SpaceMeet",
        .rule_id = CETTA_HES_RULE_SPACE_MEET,
        .profiles = "he-extended prime petta-extended",
        .provenance =
            "Lean:Mettapedia.GSLT.Dynamics.SpaceQueryAlgebra.sMeet_assoc",
        .claim = "bag-tested-adequate",
        .live = false,
    },
    {
        /* Extended full-evaluator metadata: pipe in a space position folds
         * pointwise additive union. */
        .name = "HES_SpaceAdditiveUnion",
        .rule_id = CETTA_HES_RULE_SPACE_ADDITIVE_UNION,
        .profiles = "he-extended prime petta-extended",
        .provenance =
            "Lean:Mettapedia.GSLT.Dynamics.SpaceQueryAlgebra.weightedAnswers_union",
        .claim = "bag-tested-adequate",
        .live = false,
    },
    {
        /* Extended full-evaluator metadata: two-argument sourced match is
         * the seed-threaded conjunction fold across named spaces. */
        .name = "HES_SourcedConjunctionMatch",
        .rule_id = CETTA_HES_RULE_SOURCED_CONJUNCTION_MATCH,
        .profiles = "he-extended prime petta-extended",
        .provenance =
            "Lean:Mettapedia.GSLT.Dynamics.SpaceQueryAlgebra.foldConj_eq_meet",
        .claim = "bag-tested-adequate",
        .live = false,
    },
    {
        .name = "HES_QuoteQuiescent",
        .rule_id = CETTA_HES_RULE_QUOTE_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "M_SymbolOrGrounded (quote syntax)",
        .claim = "bag-tested-adequate",
        .live = false,
    },
    {
        .name = "HES_ReturnQuiescent",
        .rule_id = CETTA_HES_RULE_RETURN_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "IF_AfterArgs_Call return syntax",
        .claim = "bag-tested-adequate",
        .live = false,
    },
    {
        .name = "HES_EmptyQuiescent",
        .rule_id = CETTA_HES_RULE_EMPTY_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "M_Empty",
        .claim = "bag-tested-adequate",
        .live = false,
    },
    {
        .name = "HES_ErrorQuiescent",
        .rule_id = CETTA_HES_RULE_ERROR_QUIESCENT,
        .profiles = "he he-extended",
        .provenance = "M_Error",
        .claim = "bag-tested-adequate",
        .live = false,
    },
};

const CettaLangdefPack *cetta_langdef_pack_he_small_step(void) {
    static CettaLangdefPack pack;
    static bool initialized = false;

    if (!initialized) {
        pack.language_id = "HE";
        pack.profile_id = "he-extended";
        pack.granularity = "small-step";
        pack.schema_version = 2;
        pack.rules = he_small_step_rules;
        pack.rule_count =
            (uint32_t)(sizeof he_small_step_rules / sizeof he_small_step_rules[0]);
        cetta_langdef_pack_finalize(&pack);
        initialized = true;
    }
    return &pack;
}
