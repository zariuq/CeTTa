!(import! &self interaction)
!(import! &self open-rule-source.metta)
!(bind! &workspace (new-space))
!(let $source (sr:source "1") (add-atom &workspace (current-source $source)))
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)) ($goal (sr:b))
        ($program (sr:program)))
   (add-atom &workspace (proof-wait "job" $source $context $context $goal $program)))

; The service may reply later. The continuation keeps the original source,
; contexts, goal and rule program; proposed arguments are independently checked.
!(await (transform &workspace
   (, (read (current-source $source))
      (take (proof-wait $id $expected-source $premises $ambient $goal $program))
      (take (proof-arguments $id $arguments))
      (let True (== $source $expected-source))
      (let (Some $rule) (gslt-open-rule-value-v1 (GJudgmentRequest $source $premises $goal) $program))
      (let (Some $result) (gslt-open-apply-value-v1 $rule $ambient $arguments)))
   (proof-evidence $id $result) &workspace))
!(assertEqual (collapse (match &workspace (proof-evidence "job" $result) $result)) ())
!(assertEqual (collapse (match &workspace (proof-wait "job" $s $p $a $g $r) pending)) (pending))

; An unsuccessful proposal leaves both the work and its data inspectable.
!(transform &workspace () (proof-arguments "job" PrNil) &workspace)
!(assertEqual (collapse (match &workspace (proof-evidence "job" $result) $result)) ())
!(assertEqual (collapse (match &workspace (proof-wait "job" $s $p $a $g $r) pending)) (pending))

; A later valid reply completes exactly once through the same waiting rule.
!(transform &workspace () (proof-arguments "job" (PrCons (GOpenHyp 0) PrNil)) &workspace)
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)) ($goal (sr:b)))
   (assertEqual (collapse (match &workspace (proof-evidence "job" $proof)
     (gslt-open-check-request-v1 (GJudgmentRequest $source $context $goal) $proof))) (True)))
!(assertEqual (collapse (match &workspace (proof-wait "job" $s $p $a $g $r) pending)) ())
!(transform &workspace () (proof-arguments "job" (PrCons (GOpenHyp 0) PrNil)) &workspace)
!(assertEqual (size-atom (collapse (match &workspace (proof-evidence "job" $proof) $proof))) 1)
!(assertEqual (size-atom (collapse (match &workspace (current-source $source) $source))) 1)
