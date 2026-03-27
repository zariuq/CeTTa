; once: pick the first non-empty evaluation result.

(= (coin) (superpose (heads tails)))
(= (identity $x) $x)

!(assertEqualToResult (once (coin)) (heads))
!(assertEqual (once (identity (a b c))) (a b c))
!(assertEqual (once (superpose ())) Empty)
