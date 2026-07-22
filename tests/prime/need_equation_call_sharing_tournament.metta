; Current-characterization probe, not a ratified equation law.
;
; The two candidates demand disjoint arguments.  The present monolithic
; frontier forces the argument demanded by the first declared candidate, then
; carries the other candidate through those branches.  Reversing declaration
; order therefore changes which answer is duplicated.

(= (prime-call:cross A $y) left)
(= (prime-call:cross $x B) right)
!(prime-call:cross (superpose (A X)) (superpose (B Y)))

(= (prime-call:cross-reversed $x B) right-reversed)
(= (prime-call:cross-reversed A $y) left-reversed)
!(prime-call:cross-reversed (superpose (A X)) (superpose (B Y)))
