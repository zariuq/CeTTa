; select: evaluator-backed committed choice over a nondeterministic stream.

(= (coin) (superpose (heads tails)))
(= (identity $x) $x)

!(assertEqualToResult (select (coin)) (heads))
!(assertEqual (select (identity (a b c))) (a b c))
!(assertEqual (select (superpose ())) Empty)
!(assertEqual (select 2 (coin)) (heads tails))
!(assertEqual (select 5 (coin)) (heads tails))
!(assertEqual (select 0 (coin)) ())
