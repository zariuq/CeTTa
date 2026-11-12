!(import! &self math_space_store)
!(import! &self pf)
!(bind! &hol (math:new "hol"))
!(math:goal &hol "identity" hol (quote (all prop (lam P (imp P P)))) "Each proposition implies itself.")
!(math:propose &hol "wrong" "identity" "missing-introduction" (quote (pf:all-intro (pf:hyp 0))))
!(chain (math:check &hol "wrong" 100000) $attempt
   (assertEqual (case (math:attempt-status &hol "wrong")
      (((rejected $reason) RejectedEvidence) ($other Wrong))) RejectedEvidence))
!(math:propose &hol "short" "identity" "introduction"
   (quote (pf:all-intro (pf:imp-intro (pf:hyp 0)))))
!(chain (math:check &hol "short" 1) $attempt
   (assertEqual (case (math:attempt-status &hol "short")
      (((unfinished $reason) Unfinished) ($other Wrong))) Unfinished))
!(chain (math:retry &hol "short" "proof" 100000) $attempt
   (assertEqual (math:attempt-status &hol "proof") checked))
!(assertEqual (math:publish &hol "proof" hol-identity 100000)
   (Math:Published hol-identity "proof"))
!(assertEqual (math:goal-status &hol "identity" 100000)
   (Math:GoalStatus "identity" checked (published hol-identity)))
!(let $text (math:serialize &hol)
   (assertEqual
     (case (math:restore $text 100000)
       (((Math:Restored $restored) (math:goal-status $restored "identity" 100000)) ($other Wrong)))
     (Math:GoalStatus "identity" checked (published hol-identity))))
!(let $theory (math:theory &hol)
   (assertEqual (car-atom (try (hol:proves $theory
       (pf:all-intro (pf:imp-intro (pf:hyp 0))) (all prop (lam P (imp P P))) 0))) Incomplete))
!(let $theory (math:theory &hol)
   (assertEqual (hol:proves $theory (pf:all-intro (pf:imp-intro (pf:hyp 0)))
      (all prop (lam P (imp P P)))) True))
; Friendly authoring syntax is expanded by a chosen programme, not executed
; as host code by the proof checker.
!(let* (($expanded (pf:expand (pf:fix P (pf:assume h h)))) ($proof (math:code $expanded)))
   (chain (math:attempt &hol "friendly" "identity" "proof-builder" $proof 100000) $attempt
     (assertEqual (math:attempt-status &hol "friendly") checked)))
; The material profile shares the optional query budget and retains its
; separate theory selection.
!(bind! &material (new-space))
!(add-atom &material (set:profile prime-common))
!(assertEqual (set:proves &material (pf:all-intro (pf:imp-intro (pf:hyp 0)))
   (all prop (lam P (imp P P))) 100000) True)
!(assertEqual (car-atom (try (set:proves &material (pf:all-intro (pf:imp-intro (pf:hyp 0)))
   (all prop (lam P (imp P P))) 0))) Incomplete)
