; select: evaluator-backed committed choice over a nondeterministic stream.

(= (coin) (superpose (heads tails)))
(= (identity $x) $x)

!(assertEqualToResult (select (coin)) (heads))
!(assertEqual (select (identity (a b c))) (a b c))
!(assertEqual (select (superpose ())) Empty)
!(assertEqualToResult (select (superpose (Empty hit))) (hit))
!(assertEqual (select 2 (coin)) (heads tails))
!(assertEqual (select 2 (superpose (a Empty b c))) (a b))
!(assertEqual (select 5 (coin)) (heads tails))
!(assertEqual (select 0 (coin)) ())
