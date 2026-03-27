; once is kept only as a compatibility alias for select.

(= (coin) (superpose (heads tails)))

!(assertEqual (once (coin)) (select (coin)))
!(assertEqual (once (superpose ())) (select (superpose ())))
