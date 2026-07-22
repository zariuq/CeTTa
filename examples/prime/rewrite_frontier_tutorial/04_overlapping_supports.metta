; Chapter 4: overlapping supports are the unresolved case
;
; These candidates need {arg1}, {arg2}, and {arg1,arg2}.  Grouping only equal
; demand sets is insufficient: the third rule overlaps both singleton groups.
; A complete support-directed evaluator should compute each source outcome
; family once, let candidates subscribe to the observations they require, and
; publish each answer from its least dependency configuration.

(= (rewrite-tutorial:overlap A $unused) left)
(= (rewrite-tutorial:overlap $unused B) right)
(= (rewrite-tutorial:overlap A B) both)

!(rewrite-tutorial:overlap
   (superpose (A X))
   (superpose (B Y)))

; Returned values alone do not reveal duplicate producer evaluation or excess
; dependencies.  The focused tournament therefore checks per-answer event and
; receipt identity with `--emit-prime-need-trace`; stdout order is not used as
; the semantic criterion.
