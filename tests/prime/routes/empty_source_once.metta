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

; 4. A collected stream calls a captured predicate.  Each rejected input
; completes with no answer; neither its effect nor its invocation is replayed.
!(let $predicate (lam x (let () (println! lambda-false) False))
   (collapse (let $x (superpose (1 2))
      (if ($predicate $x) $x (empty)))))

; 5. A successful captured predicate also runs once for each source occurrence.
!(let $predicate (lam x (let () (println! lambda-true) True))
   (collapse (let $x (superpose (1 2))
      (if ($predicate $x) $x (empty)))))
