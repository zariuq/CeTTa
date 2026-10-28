!(import! &self interaction)
!(import! &self open-rule-source.metta)

; A current source is read, pending work is taken, and checked evidence is
; produced in one transform commit. Stored records remain recheckable data.
(= (proof-step $space)
   (transform $space
     (, (read (current-source $source))
        (take (proof-work $id $expected-source $premises $ambient $goal $program $arguments))
        (let True (== $source $expected-source))
        (let (Some $rule) (gslt-open-rule-value-v1
          (GJudgmentRequest $source $premises $goal) $program))
        (let (Some $result) (gslt-open-apply-value-v1 $rule $ambient $arguments)))
     (proof-evidence $id $result) $space))

(= (queue-proof $space $id $source $goal)
   (let $context (LCons (sr:a) LNil)
     (add-atom $space (proof-work $id $source $context $context $goal
       (sr:program) (PrCons (GOpenHyp 0) PrNil)))))

!(bind! &proofs (new-space))
!(let $source (sr:source "1") (add-atom &proofs (current-source $source)))
!(queue-proof &proofs "first" (sr:source "1") (sr:b))
!(assertEqual (proof-step &proofs) 1)
!(assertEqual (size-atom (collapse (match &proofs (current-source $source) $source))) 1)
!(assertEqual (collapse (match &proofs
   (proof-work "first" $s $p $a $g $r $args) pending)) ())
!(assertEqual (collapse (proof-step &proofs)) ())
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)) ($goal (sr:b)))
   (assertEqual (collapse (match &proofs (proof-evidence "first" $proof)
     (gslt-open-check-request-v1 (GJudgmentRequest $source $context $goal) $proof))) (True)))

; An incorrect conclusion does not consume the pending work or publish.
!(queue-proof &proofs "wrong" (sr:source "1") (sr:a))
!(assertEqual (collapse (proof-step &proofs)) ())
!(assertEqual (collapse (match &proofs
   (proof-work "wrong" $s $p $a $g $r $args) pending)) (pending))
!(assertEqual (collapse (match &proofs (proof-evidence "wrong" $proof) $proof)) ())

; A source change leaves the earlier job pending. Retrying against the
; explicitly restored source obtains a fresh read and may then commit.
!(queue-proof &proofs "stale" (sr:source "1") (sr:b))
!(let $new (sr:source "2")
   (transform &proofs (take (current-source $old)) (current-source $new) &proofs))
!(assertEqual (collapse (proof-step &proofs)) ())
!(assertEqual (collapse (match &proofs (proof-evidence "stale" $proof) $proof)) ())
!(let $old (sr:source "1")
   (transform &proofs (take (current-source $new)) (current-source $old) &proofs))
!(assertEqual (proof-step &proofs) 1)
!(assertEqual (collapse (proof-step &proofs)) ())

; A data label cannot mint a theorem. Consumers check the actual record.
!(let* (($source (sr:source "1")) ($context (LCons (sr:a) LNil)) ($goal (sr:b)))
   (assertEqual (gslt-open-check-request-v1 (GJudgmentRequest $source $context $goal)
     (GDerivation $source $context $goal (GOpenHyp 0))) False))
