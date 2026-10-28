!(import! &self interaction)
!(import! &self open-rule-source.metta)

; The same native negative-query continuation runs a real proof checker.
; Absence of a veto never replaces the positive check before publication.
(= (query-proof-step $space)
   (transform $space
     (, (read (current-source $source))
        (take (proof-work $id $expected-source $premises $ambient $goal $program $arguments))
        (let True (== $source $expected-source))
        (let (Some $rule) (gslt-open-rule-value-v1
          (GJudgmentRequest $source $premises $goal) $program))
        (let (Some $result) (gslt-open-apply-value-v1 $rule $ambient $arguments))
        (absent (, (read (proof-veto $id $proposal))
                   (let False (gslt-open-check-request-v1
                     (GJudgmentRequest $source $ambient $goal) $proposal)))))
     (proof-evidence $id $result) $space))

(= (queue-query-proof $space $id $goal)
   (let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)))
     (add-atom $space (proof-work $id $source $context $context $goal
       (sr:program) (PrCons (GOpenHyp 0) PrNil)))))

!(bind! &query-proofs (new-space))
!(let $source (sr:source "1") (add-atom &query-proofs (current-source $source)))
!(queue-query-proof &query-proofs "first" (sr:b))
!(assertEqual (query-proof-step &query-proofs) 1)
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)))
   (assertEqual (collapse (match &query-proofs (proof-evidence "first" $proof)
     (gslt-open-check-request-v1 (GJudgmentRequest $source $context (sr:b)) $proof))) (True)))
!(assertEqual (collapse (query-proof-step &query-proofs)) ())

; A forged conclusion still fails the positive checker when there is no veto.
!(queue-query-proof &query-proofs "wrong" (sr:a))
!(assertEqual (collapse (query-proof-step &query-proofs)) ())
!(assertEqual (collapse (match &query-proofs (proof-evidence "wrong" $proof) $proof)) ())
!(assertEqual (collapse (match &query-proofs
   (proof-work "wrong" $s $p $a $g $r $args) pending)) (pending))

; A malformed alternative is checked inside the nested absence computation.
!(queue-query-proof &query-proofs "blocked" (sr:b))
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)))
   (add-atom &query-proofs (proof-veto "blocked"
     (GDerivation $source $context (sr:b) (GOpenHyp 0)))))
!(assertEqual (collapse (query-proof-step &query-proofs)) ())
!(assertEqual (collapse (match &query-proofs (proof-evidence "blocked" $proof) $proof)) ())
!(assertEqual (collapse (match &query-proofs
   (proof-work "blocked" $s $p $a $g $r $args) pending)) (pending))

; Replacing the invalid alternative with independently checked evidence
; makes the negative query complete without an invalid row.
!(let $_ (transform &query-proofs (take (proof-veto "blocked" $proof)) ())
   (assertEqual (collapse (match &query-proofs (proof-veto "blocked" $proof) $proof)) ()))
!(match &query-proofs (proof-evidence "first" $proof)
   (add-atom &query-proofs (proof-veto "blocked" $proof)))
!(assertEqual (query-proof-step &query-proofs) 1)
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)))
   (assertEqual (collapse (match &query-proofs (proof-evidence "blocked" $proof)
     (gslt-open-check-request-v1 (GJudgmentRequest $source $context (sr:b)) $proof))) (True)))
!(assertEqual (collapse (query-proof-step &query-proofs)) ())
