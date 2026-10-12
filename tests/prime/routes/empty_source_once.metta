; A `let` whose source gives no answer computes that source once, on every
; route: an empty answer is complete, so the source is not computed again,
; and its effects run once.  A query with no answer prints no line.

(= (pe $x) (let $u (println! $x) (empty)))

; 1. A choice of two sources, neither of which answers.
(= (w) (let $x (superpose ((pe a) (pe b))) $x))
!(once (w))

; 2. One source that does not answer.
(= (w3) (let $x (pe c) $x))
!(once (w3))

; 3. The whole result is observed: the same sources, once each.
!(collapse (w))
