; The provenance forest over the provenance library: Poker's generalisation of
; a^n b^n by steps, with its constraints written as a function.  (Reuse by
; labels is checked on the recorded problems in tests/provenance/tasks.)
!(import! &self provenance)
!(import! &self "../../lib/ilp/forest.metta")
(m a (cell a $t) $t)
(m b (cell b $t) $t)
(m empty $x $x)
(metarule identity $P (sub identity $P $Q) (m $P $x $y) ((m $Q $x $y)))
(metarule chain $P (sub chain $P $Q $R) (m $P $x $y) ((m $Q $x $z) (m $R $z $y)))
(= (forest:signature) (s inv_1))
(= (forest:allowed (sub identity $P $Q)) (if (== $P $Q) False True))
(= (forest:allowed (sub chain $P $Q $R)) (if (== $P $Q) False (if (== $Q $R) False True)))
; a^2 b^2 within three steps: the four sub-hypotheses Poker's prover finds
!(forest:generalise (m s (cell a (cell a (cell b (cell b nil)))) nil) (steps 3))
; their numbers for a^n b^n, n = 1 to 4, within 2n - 1 steps
!(forest:generalise-all ((m s (cell a (cell b nil)) nil)) (steps 1))
!(let $r (forest:generalise (m s (cell a (cell a (cell a (cell b (cell b (cell b nil)))))) nil) (steps 5)) (size-atom $r))
!(let $r (forest:generalise (m s (cell a (cell a (cell a (cell a (cell b (cell b (cell b (cell b nil)))))))) nil) (steps 7)) (size-atom $r))
; any string the background parses can be generalised, b a too
!(let $r (forest:generalise (m s (cell b (cell a nil)) nil) (steps 3)) (size-atom $r))
; a letter the background does not know leaves no refutation
!(forest:generalise (m s (cell c (cell b nil)) nil) (steps 3))
