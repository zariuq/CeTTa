; First answer.  Racing two accepted candidates: either may be the answer
; under the default promise; order is pinned only by a requested strategy;
; effects are counted separately from answer membership; exhaustion is never
; refutation.
!(bind! &log (new-space))
(= (candidate $v) (let $_ (add-atom &log (ran $v)) $v))
(= (race) (superpose ((candidate p) (candidate q))))
; 1. Membership: the first answer is one of the accepted answers.
!(let $a (once (race)) (or (== $a p) (== $a q)))
; 2. Effects: how many candidates ran to produce that one answer.  One:
;    `once` takes the branches of the race one at a time and stops at its
;    first answer, so the candidate after it never runs.
!(collapse (match &log (ran $v) $v))
; 3. Exhaustion is not refutation: an empty race has no first answer, and
;    try says so without claiming the candidates were refuted.
(= (empty-race) (superpose ()))
!(try (once (empty-race)))
; 4. Under the whole-set promise, both answers, in some order.
!(unique (collapse (race)))
