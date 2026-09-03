!(import! &self langdef)

(= (test:p0 $head)
   (PApp $head LNil))

(= (test:p1 $head $first)
   (PApp $head (LCons $first LNil)))

(= (test:p2 $head $first $second)
   (PApp $head (LCons $first (LCons $second LNil))))

(= (test:congruence-result
      (LangDef:ContextualResults LangDef:ContextComplete
        (LangDef:ContextualResult
          (PApp "tptp-fof-named:terms-cons"
            (LCons
              (PApp "tptp-fof-named:term-variable"
                (LCons
                  (PApp "tptp-fof-named:name"
                    (LCons (PApp "X" LNil) LNil))
                  LNil))
              (LCons (PApp "tptp-fof-named:terms-nil" LNil) LNil)))
          (LangDef:RuleTrace 15 "tptp-fof-elab:arguments-one"
            (LangDef:CongruenceEvidence 0
              (LangDef:RuleTrace 0 "tptp-fof-elab:term-variable"))))))
   True)

(= (test:relation-result
      (LangDef:ContextualResults LangDef:ContextComplete
        (LangDef:ContextualResult
          (PApp "tptp-fof-resolved:index-zero" LNil)
          (LangDef:RuleTrace 0 "tptp-fof-resolve:lookup-cons"
            (LangDef:RelationEvidence
              0 LangDef:ExternalRelation 0 $receipt)
            (LangDef:CongruenceEvidence 1
              (LangDef:RuleTrace 1 "tptp-fof-resolve:lookup-equal"))))))
   $receipt)

(= (test:duplicate-relation-results
      (LangDef:ContextualResults LangDef:ContextComplete
        (LangDef:ContextualResult $term
          (LangDef:RuleTrace 0 "tptp-fof-resolve:lookup-cons"
            (LangDef:RelationEvidence
              0 LangDef:ExternalRelation 0 1000)
            (LangDef:CongruenceEvidence 1
              (LangDef:RuleTrace 1 "tptp-fof-resolve:lookup-equal"))))
        (LangDef:ContextualResult $term
          (LangDef:RuleTrace 0 "tptp-fof-resolve:lookup-cons"
            (LangDef:RelationEvidence
              0 LangDef:ExternalRelation 1 1001)
            (LangDef:CongruenceEvidence 1
              (LangDef:RuleTrace 1 "tptp-fof-resolve:lookup-equal"))))))
   True)

(= (test:term-normalization-result
      (LangDef:ContextualTermResults LangDef:ContextComplete
        (LangDef:ContextualTermResult
          (tptp-fof-nnf:verum)
          (LangDef:RuleTrace 0 "tptp-fof-normalize:positive-verum"))))
   True)

(= (test:term-relation-result
      (LangDef:ContextualTermResults LangDef:ContextComplete
        (LangDef:ContextualTermResult
          (tptp-fof-resolved:index-zero)
          (LangDef:RuleTrace 0 "tptp-fof-resolve:lookup-cons"
            (LangDef:RelationEvidence
              0 LangDef:ExternalRelation 0 $receipt)
            (LangDef:CongruenceEvidence 1
              (LangDef:RuleTrace 1 "tptp-fof-resolve:lookup-equal"))))))
   $receipt)

(= (test:duplicate-term-relation-results
      (LangDef:ContextualTermResults LangDef:ContextComplete
        (LangDef:ContextualTermResult $term
          (LangDef:RuleTrace 0 "tptp-fof-resolve:lookup-cons"
            (LangDef:RelationEvidence
              0 LangDef:ExternalRelation 0 1000)
            (LangDef:CongruenceEvidence 1
              (LangDef:RuleTrace 1 "tptp-fof-resolve:lookup-equal"))))
        (LangDef:ContextualTermResult $term
          (LangDef:RuleTrace 0 "tptp-fof-resolve:lookup-cons"
            (LangDef:RelationEvidence
              0 LangDef:ExternalRelation 1 1001)
            (LangDef:CongruenceEvidence 1
              (LangDef:RuleTrace 1 "tptp-fof-resolve:lookup-equal"))))))
   True)

(= (test:target-codec-fault
      (LangDef:ContextualCodecFault
        LangDef:TargetTerm unknown_type $message))
   True)

(= (test:source-codec-fault
      (LangDef:ContextualCodecFault
        LangDef:SourceTerm unknown_constructor $message))
   True)

!(bind! &official-to-named
   (langdef:load-language "langdef/tptp/official_fof_to_named_v1.metta"))

!(bind! &official-variable
   (test:p1 "tptp92-ast:fof-term:alt-2"
     (test:p1 "tptp92-ast:variable:alt-1"
       (test:p1 "tptp92-ast:token:upper-word"
         (test:p0 "X")))))

!(bind! &arguments-source
   (test:p1 "tptp-fof-elab:arguments"
     (test:p1 "tptp92-ast:fof-arguments:alt-1"
       &official-variable)))

!(bind! &congruence-result
   (langdef:rewrite-pattern-at
     &official-to-named 2 200000 (LangDef:RelationRows)
     &arguments-source))
!(assertEqual (test:congruence-result &congruence-result) True)

!(bind! &fuel-result
   (langdef:rewrite-pattern-at
     &official-to-named 1 200000 (LangDef:RelationRows)
     &arguments-source))
!(assertEqual
   &fuel-result
   (LangDef:ContextualResults LangDef:ContextFuelExhausted))

!(bind! &resolved
   (langdef:load-language "langdef/tptp/named_fof_to_resolved_v1.metta"))

!(bind! &lookup-source
   (test:p2 "tptp-fof-resolve:lookup"
     (test:p0 "X")
     (test:p2 "tptp-fof-resolve:environment-cons"
       (test:p0 "X")
       (test:p0 "tptp-fof-resolve:environment-nil"))))

!(bind! &equality-row
   (LangDef:RelationRow "PatternEqualityDecision" 1000
     (test:p0 "X")
     (test:p0 "X")
     (test:p0 "pattern-equality-decision:equal")))

!(bind! &duplicate-equality-row
   (LangDef:RelationRow "PatternEqualityDecision" 1001
     (test:p0 "X")
     (test:p0 "X")
     (test:p0 "pattern-equality-decision:equal")))

!(bind! &relation-result
   (langdef:rewrite-pattern-at
     &resolved 2 200000 (LangDef:RelationRows &equality-row)
     &lookup-source))
!(assertEqual (test:relation-result &relation-result) 1000)

!(bind! &duplicate-result
   (langdef:rewrite-pattern-at
     &resolved 2 200000
     (LangDef:RelationRows
       &equality-row
       &duplicate-equality-row)
     &lookup-source))
!(assertEqual (test:duplicate-relation-results &duplicate-result) True)

!(bind! &absent-result
   (langdef:rewrite-pattern-at
     &resolved 2 200000 (LangDef:RelationRows)
     &lookup-source))
!(assertEqual
   &absent-result
   (LangDef:ContextualResults LangDef:ContextComplete))

!(bind! &malformed-provider-result
   (langdef:rewrite-pattern-at
     &resolved 2 200000
     (LangDef:RelationRows
       (LangDef:RelationRow "PatternEqualityDecision" 1002 (Var 0)))
     &lookup-source))
!(assertEqual
   &malformed-provider-result
   (LangDef:ContextualFault ContextualRelationProviderMalformed
     "contextual runner accepts only PApp/FVar Pattern wires"))

!(bind! &normalization
   (langdef:load-language "langdef/tptp/fof_normalization_v1.metta"))

!(bind! &term-normalization-result
   (langdef:rewrite-term-at
     &normalization NNFFormula NNFFormula
     1 200000 (LangDef:RelationRows)
     (tptp-fof-normalize:positive
       (tptp-fof-resolved:verum))))
!(assertEqual
   (test:term-normalization-result &term-normalization-result)
   True)

!(bind! &term-normal-form-result
   (langdef:rewrite-term-at
     &normalization NNFFormula NNFFormula
     1 200000 (LangDef:RelationRows)
     (tptp-fof-nnf:verum)))
!(assertEqual
   &term-normal-form-result
   (LangDef:ContextualTermResults LangDef:ContextComplete))

; Even an empty reduct list cannot hide an unknown result category.
!(bind! &unknown-target-result
   (langdef:rewrite-term-at
     &normalization NNFFormula InventedTarget
     1 200000 (LangDef:RelationRows)
     (tptp-fof-nnf:verum)))
!(assertEqual (test:target-codec-fault &unknown-target-result) True)

!(bind! &invented-source-result
   (langdef:rewrite-term-at
     &normalization NNFFormula NNFFormula
     1 200000 (LangDef:RelationRows)
     (tptp-fof-normalize:positive
       (tptp-fof-resolved:invented))))
!(assertEqual (test:source-codec-fault &invented-source-result) True)

!(bind! &term-lookup-source
   (tptp-fof-resolve:lookup "X"
     (tptp-fof-resolve:environment-cons "X"
       (tptp-fof-resolve:environment-nil))))

!(bind! &term-relation-result
   (langdef:rewrite-term-at
     &resolved TptpResolvedFof:Index TptpResolvedFof:Index
     2 200000 (LangDef:RelationRows &equality-row)
     &term-lookup-source))
!(assertEqual
   (test:term-relation-result &term-relation-result)
   1000)

!(bind! &duplicate-term-result
   (langdef:rewrite-term-at
     &resolved TptpResolvedFof:Index TptpResolvedFof:Index
     2 200000
     (LangDef:RelationRows &equality-row &duplicate-equality-row)
     &term-lookup-source))
!(assertEqual
   (test:duplicate-term-relation-results &duplicate-term-result)
   True)

!(assertEqual (langdef:close-language &normalization) True)
!(assertEqual (langdef:close-language &resolved) True)
!(assertEqual (langdef:close-language &official-to-named) True)
!(println! (LanguageDefContextualRuntimeV1Summary 15 15 0))
