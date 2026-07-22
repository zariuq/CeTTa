; Chapter 3: monolithic is a candidate-scheduling policy
;
; `monolithic` is not a kind of equality.  It is the current scheduling policy
; for multiple directional rewrite-rule candidates of one call.
;
; The two rules below inspect disjoint arguments:
;
;   left rule  needs argument 1 and ignores argument 2
;   right rule needs argument 2 and ignores argument 1
;
; A monolithic frontier carries both pending candidates through one shared
; decision tree.  Forcing argument 1 can therefore carry the right candidate
; through choices it never observes, multiplying an answer.  Reversing rule
; declaration order can reverse which candidate is multiplied.

(= (rewrite-tutorial:left-first A $unused) left)
(= (rewrite-tutorial:left-first $unused B) right)
!(rewrite-tutorial:left-first
   (superpose (A X))
   (superpose (B Y)))

(= (rewrite-tutorial:right-first $unused B) right-reversed)
(= (rewrite-tutorial:right-first A $unused) left-reversed)
!(rewrite-tutorial:right-first
   (superpose (A X))
   (superpose (B Y)))

; Run this same source under:
;
;   --prime-rewrite-frontier monolithic
;   --prime-rewrite-frontier candidate-local
;   --prime-rewrite-frontier demand-cohort
;
; Candidate-local separates every rule and therefore loses sharing between
; candidates.  Demand-cohort groups rules with exactly equal demanded supports.
; Neither experimental alternative is the selected production semantics.
