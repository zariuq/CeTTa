!(import! &self lib_tptp)

(= (test:stage-execution
      (TPTP:StageExecutionV1
        fof-positive-normalization $wire-sha256
        (tptp-fof-normalize:positive (tptp-fof-resolved:verum))
        (LangDef:ContextualTermResults LangDef:ContextComplete
          (LangDef:ContextualTermResult
            (tptp-fof-nnf:verum)
            (LangDef:RuleTrace
              $_index "tptp-fof-normalize:positive-verum")))))
   (TptpContextualStageReceipt.ExecutionRetained
     fof-positive-normalization $wire-sha256))

(= (test:single-stage-value
      (TPTP:TransformationValueV1
        (tptp-fof-nnf:verum)
        (TPTP:TransformationReceiptV1
          fof-positive-normalization $wire-sha256
          (tptp-fof-normalize:positive (tptp-fof-resolved:verum))
          (tptp-fof-nnf:verum)
          (LangDef:RuleTrace
            $_index "tptp-fof-normalize:positive-verum"))))
   (TptpContextualStageReceipt.SingletonRetained
     fof-positive-normalization $wire-sha256))

(= (test:rejected
      (TPTP:StageRejected $stage $reason $_detail))
   (TptpContextualStageReceipt.Rejected $stage $reason))

(= (test:congruence-stage-execution
      (TPTP:StageExecutionV1
        official-fof-to-named $wire-sha256 $_request
        (LangDef:ContextualTermResults LangDef:ContextComplete
          (LangDef:ContextualTermResult
            (tptp-fof-named:verum)
            (LangDef:RuleTrace $_formula-index
              "tptp-fof-elab:formula-logic"
              (LangDef:CongruenceEvidence 0
                (LangDef:RuleTrace $_logic-index
                  "tptp-fof-elab:logic-unitary"
                  (LangDef:CongruenceEvidence 0
                    (LangDef:RuleTrace $_unitary-index
                      "tptp-fof-elab:unitary-atomic"
                      (LangDef:CongruenceEvidence 0
                        (LangDef:RuleTrace $_atomic-index
                          "tptp-fof-elab:atomic-defined"
                          (LangDef:CongruenceEvidence 0
                            (LangDef:RuleTrace $_defined-index
                              "tptp-fof-elab:defined-atomic-plain"
                              (LangDef:CongruenceEvidence 0
                                (LangDef:RuleTrace $_truth-index
                                  "tptp-fof-elab:defined-true")))))))))))))))
   (TptpContextualStageReceipt.CongruenceRetained $wire-sha256))

(= (test:relation-stage-execution
      (TPTP:StageExecutionV1
        named-fof-to-resolved $wire-sha256 $_request
        (LangDef:ContextualTermResults LangDef:ContextComplete
          (LangDef:ContextualTermResult $_target
            (LangDef:RuleTrace $_closed-index
              "tptp-fof-resolve:closed"
              (LangDef:CongruenceEvidence 0
                (LangDef:RuleTrace $_all-index
                  "tptp-fof-resolve:formula-all"
                  (LangDef:CongruenceEvidence 0
                    (LangDef:RuleTrace $_equal-index
                      "tptp-fof-resolve:formula-equal"
                      (LangDef:CongruenceEvidence 0
                        (LangDef:RuleTrace $_term-index
                          "tptp-fof-resolve:term-variable"
                          (LangDef:CongruenceEvidence 0
                            (LangDef:RuleTrace $_lookup-index
                              "tptp-fof-resolve:lookup-cons"
                              (LangDef:RelationEvidence
                                0 LangDef:ExternalRelation 0 $receipt)
                              $_lookup-continuation))))
                      $_right-term)))))))))
   (TptpContextualStageReceipt.RelationRetained
     $wire-sha256 $receipt))

!(bind! &execution
   (tptp:language-stage-execution
     fof-positive-normalization
     "langdef/tptp/fof_normalization_v1.metta"
     NNFFormula NNFFormula
     1 200000 (LangDef:RelationRows)
     (tptp-fof-normalize:positive (tptp-fof-resolved:verum))))
!(assertEqual
   (test:stage-execution &execution)
   (TptpContextualStageReceipt.ExecutionRetained
     fof-positive-normalization
     "c75961cd0b065760394fa8e42a157979560fa7f510fbcabdeee5b4d0c7bdfda0"))
!(assertEqual
   (test:single-stage-value
     (tptp:require-single-stage-execution &execution))
   (TptpContextualStageReceipt.SingletonRetained
     fof-positive-normalization
     "c75961cd0b065760394fa8e42a157979560fa7f510fbcabdeee5b4d0c7bdfda0"))

!(assertEqual
   (test:rejected
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-positive-normalization
         "langdef/tptp/fof_normalization_v1.metta"
         NNFFormula NNFFormula
         1 200000 (LangDef:RelationRows)
         (tptp-fof-nnf:verum))))
   (TptpContextualStageReceipt.Rejected
     fof-positive-normalization NoReduct))

!(assertEqual
   (test:rejected
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-positive-normalization
         "langdef/tptp/fof_normalization_v1.metta"
         NNFFormula NNFFormula
         0 200000 (LangDef:RelationRows)
         (tptp-fof-normalize:positive
           (tptp-fof-resolved:verum)))))
   (TptpContextualStageReceipt.Rejected
     fof-positive-normalization ContextFuelExhausted))

!(bind! &congruence-execution
   (tptp:language-stage-execution
     official-fof-to-named
     "langdef/tptp/official_fof_to_named_v1.metta"
     TptpNamedFof:Formula TptpNamedFof:Formula
     32 500000 (LangDef:RelationRows)
     (tptp-fof-elab:formula
       (tptp92-ast:fof-formula:alt-1
         (tptp92-ast:fof-logic-formula:alt-3
           (tptp92-ast:fof-unitary-formula:alt-2
             (tptp92-ast:fof-atomic-formula:alt-2
               (tptp92-ast:fof-defined-atomic-formula:alt-1
                 (tptp92-ast:fof-defined-plain-formula:alt-1
                   (tptp92-ast:fof-defined-plain-term:alt-1
                     (tptp92-ast:defined-constant:alt-1
                       (tptp92-ast:defined-functor:alt-1
                         (tptp92-ast:atomic-defined-word:alt-1
                           (tptp92-ast:token:dollar-word "$true"))))))))))))))
!(assertEqual
   (test:congruence-stage-execution &congruence-execution)
   (TptpContextualStageReceipt.CongruenceRetained
     "1d8c85156429abf99dcf75aeb6176113b3eabd3d33b0ffe1cf19739f17d186b8"))

!(bind! &relation-execution
   (tptp:language-stage-execution
     named-fof-to-resolved
     "langdef/tptp/named_fof_to_resolved_v1.metta"
     TptpResolvedFof:Formula TptpResolvedFof:Formula
     32 500000
     (LangDef:RelationRows
       (LangDef:RelationRow "PatternEqualityDecision" 7100
         (PApp "X" LNil) (PApp "X" LNil)
         (PApp "pattern-equality-decision:equal" LNil)))
     (tptp-fof-resolve:closed
       (tptp-fof-named:all
         (tptp-fof-named:name "X")
         (tptp-fof-named:equal
           (tptp-fof-named:term-variable
             (tptp-fof-named:name "X"))
           (tptp-fof-named:term-variable
             (tptp-fof-named:name "X")))))))
!(assertEqual
   (test:relation-stage-execution &relation-execution)
   (TptpContextualStageReceipt.RelationRetained
     "feb0c635955c8ee457affa1f8f886e98d031e5ce417210fc50bcfdd21bae7541"
     7100))

!(println! (LibTptpContextualStageReceiptV1Summary 6 6 0))
