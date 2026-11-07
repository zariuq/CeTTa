;; A table that leads its own component in its first round can learn, while it
;; iterates, that it reads an older table still being evaluated: an answer
;; makes a call the first round did not.  Its component then belongs to the
;; older table's, and both reach their fixed point together.
;;   i(x,y) <- t(y,x).   i(x,y) <- p(y,x).   t(x,y) <- i(x,z), i(z,y).
;;   p(c0,c1).  p(c1,c1).
;; i holds for all four pairs; t(c1,c0) has two derivations (z = c1, z = c0).
!(import! &self (library lib_tabling))
!(tabled (ip $x $y))
(= (pp c0 c1) True)
(= (pp c1 c1) True)
(= (ip $x $y) (tp $y $x))
(= (ip $x $y) (pp $y $x))
(= (tp $x $y) (let True (ip $x $z) (ip $z $y)))
!(t-c1-c0 (collapse (tp c1 c0)))
!(i-c0-y (sort (collapse (let True (ip c0 $y) $y))))
!(i-x-y (sort (collapse (let True (ip $x $y) ($x $y)))))
!(i-x-c0 (sort (collapse (let True (ip $x c0) $x))))
;; Negative: no pair has a third constant.
!(i-c2-y (collapse (let True (ip c2 $y) $y)))
