; First answer.  Racing two accepted candidates: either may be the answer
; under the default promise; order is pinned only by a requested strategy;
; effects are counted separately from answer membership; exhaustion is never
; refutation.
!(bind! &log (new-space))
(= (candidate $v) (let $_ (add-atom &log (ran $v)) $v))
(= (race) (superpose ((candidate p) (candidate q))))
; 1. Membership: the first answer is one of the accepted answers.
!(let $a (once (race)) (or (== $a p) (== $a q)))
; 2. Effects: how many candidates ran to produce that one answer.  Observed:
;    both.  This evaluator's once selects from the whole set after every
;    candidate has run; it is a filter on answers, not a strategy on effects.
!(collapse (match &log (ran $v) $v))
; 3. Exhaustion is not refutation: an empty race has no first answer, and
;    try says so without claiming the candidates were refuted.
(= (empty-race) (superpose ()))
!(try (once (empty-race)))
; 4. Under the whole-set promise, both answers, in some order.
!(unique (collapse (race)))
