!(import! &self ../../../../Mettapedia/MettaKernel/kernel/generic_open_rule_programs.metta)
(= (or:a) (PApp "A" LNil))
(= (or:b) (PApp "B" LNil))
(= (or:c) (PApp "C" LNil))
(= (or:ab) (GRuleV1 "ab" LNil (LCons (or:a) LNil) (or:b) LNil))
(= (or:bc) (GRuleV1 "bc" LNil (LCons (or:b) LNil) (or:c) LNil))
(= (or:both) (GRuleV1 "combine" LNil (LCons (or:a) (LCons (or:b) LNil)) (or:c) LNil))
(= (or:language)
   (GInferenceLanguageV1 1 LNil
     (LCons (JDecl "A" 0) (LCons (JDecl "B" 0) (LCons (JDecl "C" 0) LNil)))
     (LCons (or:ab) (LCons (or:bc) (LCons (or:both) LNil))) GNoConversion))
(= (or:ab-program) (GProof (GRuleInst "ab" LNil) (PrCons (GOpenHyp 0) PrNil)))
(= (or:bc-program) (GProof (GRuleInst "bc" LNil) (PrCons (GOpenHyp 0) PrNil)))
!(let $language (or:language) (assertEqual (gic-language-valid $language) True))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b)) ($p (or:ab-program)))
   (assertEqual (gor-check $language (LCons $a LNil) $b $p) True))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b)) ($c (or:c))
         ($ab (or:ab-program)) ($bc (or:bc-program)))
   (let (Some $proof) (gor-apply $language (LCons $b LNil) (LCons $a LNil) $c $bc (PrCons $ab PrNil))
     (assertEqual (gor-check $language (LCons $a LNil) $c $proof) True)))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b)))
   (assertEqual (gor-apply $language (LCons $a (LCons $b LNil)) (LCons $a LNil) $a
     (GOpenHyp 0) (PrCons (GOpenHyp 0) PrNil)) None))
!(let* (($language (or:language)) ($a (or:a)))
   (assertEqual (gor-apply $language (LCons $a LNil) (LCons $a LNil) $a
     (GOpenHyp 0) (PrCons (GOpenHyp 0) (PrCons (GOpenHyp 0) PrNil))) None))
!(let* (($language (or:language)) ($a (or:a)))
   (assertEqual
     (gor-apply $language (LCons $a LNil) (LCons $a (LCons $a LNil)) $a
       (GOpenHyp 0) (PrCons (GOpenHyp 1) PrNil))
     (Some (GOpenHyp 1))))
!(let* (($language (or:language)) ($a (or:a)))
   (assertEqual (gor-check $language (LCons $a LNil) $a (GOpenHyp 1)) False))
!(let* (($language (or:language)) ($a (or:a)) ($c (or:c)) ($p (or:ab-program)))
   (assertEqual (gor-check $language (LCons $a LNil) $c $p) False))

; Reuse retains missing lookups too. Supplying the missing premise changes
; a negative answer, so its old manifest must refuse reuse in that context.
!(let* (($language (or:language)) ($a (or:a))
        ($manifest (gor-manifest $language LNil (GOpenHyp 0))))
   (assertEqual (gor-agrees $language (LCons $a LNil) $manifest) False))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b))
        ($manifest (gor-manifest $language (LCons $a LNil) (GOpenHyp 0))))
   (assertEqual (gor-agrees $language (LCons $b LNil) $manifest) False))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b))
        ($manifest (gor-manifest $language (LCons $a LNil) (GOpenHyp 0))))
   (assertEqual (gor-agrees $language (LCons $a (LCons $b LNil)) $manifest) True))
!(let* (($language (or:language)) ($a (or:a)) ($p (or:ab-program))
        ($manifest (gor-manifest $language (LCons $a LNil) $p)))
   (assertEqual
     (gor-agrees (GInferenceLanguageV1 1 LNil
       (LCons (JDecl "A" 0) (LCons (JDecl "B" 0) (LCons (JDecl "C" 0) LNil)))
       (LCons (or:bc) LNil) GNoConversion) (LCons $a LNil) $manifest)
     False))
; A former missing rule cannot be reused as a negative result after addition.
!(let* (($language (or:language)) ($a (or:a)) ($p (or:ab-program))
        ($manifest (gor-manifest (GInferenceLanguageV1 1 LNil LNil LNil GNoConversion)
          (LCons $a LNil) $p)))
   (assertEqual (gor-agrees $language (LCons $a LNil) $manifest) False))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b)))
   (assertEqual (gor-apply $language (LCons $b LNil) (LCons $a LNil) (or:c)
     (or:bc-program) (PrCons (GOpenHyp 0) PrNil)) None))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b)))
   (assertEqual (gor-apply $language (LCons $a (LCons $b LNil))
     (LCons $a (LCons $b LNil)) (or:c)
     (GProof (GRuleInst "combine" LNil) (PrCons (GOpenHyp 0) (PrCons (GOpenHyp 1) PrNil)))
     (PrCons (GOpenHyp 1) (PrCons (GOpenHyp 0) PrNil))) None))

; A rule builder returns an ordinary value. Its application checks the value
; again and fills its premise holes in the supplied ambient context.
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b))
        ($request (GJudgmentRequest $language (LCons $a LNil) $b))
        ($program (or:ab-program)))
   (let (Some $rule) (gor-rule-value $request $program)
     (let (Some $derived) (gor-apply-value $rule (LCons $a LNil) (PrCons (GOpenHyp 0) PrNil))
       (assertEqual (gor-check-value $derived) True))))
!(let* (($language (or:language)) ($a (or:a)) ($c (or:c)))
   (assertEqual (gor-check-value (GDerivation $language (LCons $a LNil) $c (or:ab-program))) False))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b)))
   (assertEqual (gor-check-value (GJudgmentRequest $language (LCons $a LNil) $b)) False))
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b))
        ($request (GJudgmentRequest $language (LCons $a LNil) $b)))
   (assertEqual (gor-check-request $request
     (GDerivation $language (LCons $a LNil) $b (or:ab-program))) True))
; A valid proof of another claim does not answer the requested judgment.
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b))
        ($request (GJudgmentRequest $language (LCons $a LNil) $b)))
   (assertEqual (gor-check-request $request
     (GDerivation $language (LCons $a LNil) $a (GOpenHyp 0))) False))

; The operational view retains actual rule instances and each premise use.
!(let* (($language (or:language)) ($a (or:a)) ($b (or:b)) ($c (or:c)))
   (let (Some $proof) (gor-apply $language (LCons $b LNil) (LCons $a LNil) $c
        (or:bc-program) (PrCons (or:ab-program) PrNil))
     (assertEqual (gor-checked-events $language (LCons $a LNil) $c $proof)
       (Some (LCons (GRuleEvent (GRuleInst "bc" LNil))
         (LCons (GRuleEvent (GRuleInst "ab" LNil))
           (LCons (GAssumptionEvent 0) LNil)))))))
!(let* (($language (or:language)) ($a (or:a)))
   (assertEqual (gor-checked-events $language (LCons $a (LCons $a LNil)) $a (GOpenHyp 1))
     (Some (LCons (GAssumptionEvent 1) LNil))))
!(let* (($language (or:language)) ($a (or:a)) ($c (or:c)))
   (assertEqual (gor-checked-events $language (LCons $a LNil) $c (or:ab-program)) None))
