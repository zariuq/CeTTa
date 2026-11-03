; One supplied-proof request, checked against its owned theory checkpoint.
!(import! &self interaction)
!(import! &self holkernel)

(= (hol:serve-prepared $requests $theories $results)
   (transform-prepared $requests
     (, (take (HolRequest $id $theory $nodes $root))
        (read $theories (HolTheory $theory HOLLightBase $environment))
        (let $proof (hol:check-dag-at $environment $nodes $root))
        (let (thm $hypotheses $conclusion) (hol:sequent $proof)))
     (, (add-atom $results (HolChecked $id $theory $environment $proof))
        (add-atom $requests (HolReply $id $hypotheses $conclusion)))))

; A stored tag is syntax. Only its authentic theorem under both declaration
; histories supplies checked evidence in the currently selected theory.
(= (hol:checked-sequent $theories $results $id $theory)
   (transform-prepared $results
     (, (read (HolChecked $id $theory $checked-environment $proof))
        (read $theories (HolTheory $theory HOLLightBase $current-environment))
        (let True (hol:admits-at $checked-environment $proof))
        (let True (hol:admits-at $current-environment $proof))
        (let (thm $hypotheses $conclusion) (hol:sequent $proof)))
     (thm $hypotheses $conclusion)){$id $theory})
