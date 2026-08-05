; A symbolic first argument remains rooted while the second argument runs a
; long tail loop.  This permanently discriminates the generated continuation
; and moving-root seam across HE extended, Prime, and PeTTa.
(= (build $n) (if (== $n 0) nil (c $n (build (- $n 1)))))
(= (cdr-of (c $h $t)) $t)
(= (len $l) (if (== $l nil) 0 (+ 1 (len (cdr-of $l)))))
(= (loop $n $a) (if (== $n 0) $a (loop (- $n 1) (+ $a (len (c 1 nil))))))
(= (pair $x $y) (+ $x $y))
!(pair (len (build 150)) (loop 120000 0))
; expected: [120150]
