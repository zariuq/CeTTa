; A request and an immutable theory view feed the actual native HOL checker.
; Checked evidence and its reply are published by one prepared interaction.
!(import! &self interaction)
!(import! &self holkernel)
!(import! &self hol_service)

(= (hol-service:send $region $value)
   (transform $region () (add-atom $region $value)))

(= (hol-service:run $requests $theories $results)
   (hol:serve-prepared $requests $theories $results))

!(bind! &requests (new-region space))
!(bind! &theories (new-region space))
!(bind! &results (new-region board))
!(bind! &kernel (hol:kernel))
!(bind! &environment (hol:checkpoint &kernel))
!(hol-service:send &theories (HolTheory main HOLLightBase &environment))

; Equal parent edges remain two ordered uses of the same checked premise.
!(hol-service:send &requests
   (HolRequest good main
     ((HolNode 7 (REFL (v "x" (T "bool"))) ())
      (HolNode 9 (TRANS) (7 7))) 9))
!(assertEqual (hol-service:run &requests &theories &results) ())
!(assertEqual (collapse (transform &results
     (read (HolChecked good main $environment $proof))
     (hol:sequent $proof)))
   ((thm () (A (A (c "=" (T "fun" (T "bool") (T "fun" (T "bool") (T "bool"))))
                    (v "x" (T "bool"))) (v "x" (T "bool"))))))
!(assertEqual (collapse (transform &requests (read (HolReply good $hyps $goal)) replied))
   (replied))
!(assertEqual (collapse (transform &requests (read (HolRequest good $t $n $r)) pending)) ())
; Re-running cannot duplicate the evidence or the reply.
!(assertEqual (collapse (hol-service:run &requests &theories &results)) ())
!(assertEqual (collapse (transform &results (read (HolChecked good $t $e $p)) checked))
   (checked))

; An undeclared symbol and an absent premise cannot become checked evidence.
!(hol-service:send &requests (HolRequest undeclared main
   ((HolNode 1 (REFL (c "missing" (T "bool"))) ())) 1))
!(assertEqual (collapse (hol-service:run &requests &theories &results)) ())
!(assertEqual (collapse (transform &results (read (HolChecked undeclared $t $e $p)) bad)) ())
!(assertEqual (collapse (transform &requests (read (HolRequest undeclared $t $n $r)) retained))
   (retained))
!(transform &requests (take (HolRequest undeclared $t $n $r)) ())
!(hol-service:send &requests (HolRequest missing-premise main ((HolNode 1 (TRANS) (7 7))) 1))
!(assertEqual (collapse (hol-service:run &requests &theories &results)) ())
!(assertEqual (collapse (transform &results (read (HolChecked missing-premise $t $e $p)) bad)) ())
!(transform &requests (take (HolRequest missing-premise $t $n $r)) ())
!(hol-service:send &requests (HolRequest wrong-claim main
   ((HolNode 1 (REFL (v "x" (T "bool"))) () (thm () (v "p" (T "bool"))))) 1))
!(assertEqual (collapse (hol-service:run &requests &theories &results)) ())
!(assertEqual (collapse (transform &results (read (HolChecked wrong-claim $t $e $p)) bad)) ())
!(transform &requests (take (HolRequest wrong-claim $t $n $r)) ())

; A reader facet does not authorize publishing the checked result.
!(bind! &read-results (facet &results peek))
!(hol-service:send &requests (HolRequest no-write main ((HolNode 1 (REFL (v "x" (T "bool"))) ())) 1))
!(assertEqual (case (collapse (hol-service:run &requests &theories &read-results))
   ((((Error transform (not-formed (missing-right send $handle)))) True)
    ($other False))) True)
!(assertEqual (collapse (transform &results (read (HolChecked no-write $t $e $p)) bad)) ())
!(transform &requests (take (HolRequest no-write $t $n $r)) ())

; Advancing the session cannot change what its older checkpoint declares.
!(hol:new-constant &kernel "later" (T "bool"))
!(hol-service:send &requests (HolRequest newer-symbol main ((HolNode 1 (REFL (c "later" (T "bool"))) ())) 1))
!(assertEqual (collapse (hol-service:run &requests &theories &results)) ())
!(assertEqual (collapse (transform &results (read (HolChecked newer-symbol $t $e $p)) bad)) ())
; Only an authorized change of the current theory occurrence admits it.
!(bind! &new-environment (hol:checkpoint &kernel))
!(transform &theories (take (HolTheory main HOLLightBase $old))
   (add-atom &theories (HolTheory main HOLLightBase &new-environment)))
!(assertEqual (hol-service:run &requests &theories &results) ())
!(assertEqual (collapse (transform &results
     (read (HolChecked newer-symbol main $e $p)) (hol:admits-at $e $p))) (True))

; The consumer checks authentic evidence and the current theory, rather than
; trusting a HolChecked tag or a claimed immutable environment.
!(assertEqual (collapse (let (thm $hyps $goal)
     (hol:checked-sequent &theories &results newer-symbol main) admitted)) (admitted))
!(assertEqual (collapse (hol:checked-sequent &theories &results good another-theory)) ())
!(hol-service:send &results (HolChecked forged main &new-environment
     (thm () (v "p" (T "bool")))))
!(assertEqual (collapse (hol:checked-sequent &theories &results forged main)) ())
!(transform &results (read (HolChecked good main $e $proof))
     (add-atom &results (HolChecked forged-environment main ordinary-data $proof)))
!(assertEqual (collapse (hol:checked-sequent &theories &results forged-environment main)) ())
!(transform &theories (take (HolTheory main HOLLightBase $old))
     (add-atom &theories (HolTheory main HOLLightBase &environment)))
!(assertEqual (collapse (hol:checked-sequent &theories &results newer-symbol main)) ())
!(assertEqual (collapse (let (thm $hyps $goal)
     (hol:checked-sequent &theories &results good main) admitted)) (admitted))
; Restoring the stronger theory permits the still-owned proof again.
!(transform &theories (take (HolTheory main HOLLightBase $old))
     (add-atom &theories (HolTheory main HOLLightBase &new-environment)))
!(assertEqual (collapse (let (thm $hyps $goal)
     (hol:checked-sequent &theories &results newer-symbol main) admitted)) (admitted))
!(HolWorkflow passed)
