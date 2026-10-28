!(import! &self open-rule-source.metta)
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil))
        ($goal (sr:b)) ($request (GJudgmentRequest $source $context $goal)))
   (let (Some $rule) (gslt-open-rule-value-v1 $request (sr:program))
     (let (Some $proof) (gslt-open-apply-value-v1 $rule $context (PrCons (GOpenHyp 0) PrNil))
       (assertEqual (gslt-open-check-request-v1 $request $proof) True))))

; Identical rule catalogs do not erase a source revision at this interface.
!(let* (($first (sr:source "1")) ($second (sr:source "2"))
        ($context (LCons (sr:a) LNil)) ($goal (sr:b)))
   (assertEqual (gslt-open-check-request-v1 (GJudgmentRequest $first $context $goal)
     (GDerivation $second $context $goal (sr:program))) False))
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)) ($goal (sr:b))
        ($request (GJudgmentRequest $source $context $goal)))
   (let (Some $rule) (gslt-open-rule-value-v1 $request (sr:program))
     (let (Some (GDerivation $returned $actual-context $actual-goal $proof))
          (gslt-open-apply-value-v1 $rule $context (PrCons (GOpenHyp 0) PrNil))
       (assertEqual (== $returned $source) True))))

; Source admission failure is distinct from refusal of a proposed proof.
!(let $source (GSLTSourceV1 (SourceIdentityV1 "open-rule-calibration" "1" "calibration-artifact")
     (AssumptionLedgerV1 LNil) (ProfileLedgerV1 LNil) (sr:language))
   (assertEqual (gslt-open-rule-value-v1
     (GJudgmentRequest $source (LCons (sr:a) LNil) (sr:b)) (sr:program))
     (SourceRejectedV1 MissingProfileV1)))
!(assertEqual (gslt-open-rule-value-v1
   (GJudgmentRequest (sr:source "1") (LCons (sr:a) LNil) (sr:a)) (sr:program)) None)

; An altered assumption ledger is not interchangeable with the requested one.
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)) ($goal (sr:b)))
   (assertEqual (gslt-open-check-request-v1 (GJudgmentRequest $source $context $goal)
     (GDerivation (GSLTSourceV1
       (SourceIdentityV1 "open-rule-calibration" "1" "calibration-artifact")
       (AssumptionLedgerV1 (LCons (SourceAssumptionV1 "extra" (sr:a)) LNil))
       (ProfileLedgerV1 (LCons (SourceProfileV1 "proof-calculus" "1" (PApp "ExactPremises" LNil)) LNil))
       (sr:language)) $context $goal (sr:program))) False))
