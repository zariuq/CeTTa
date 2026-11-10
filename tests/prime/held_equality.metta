; A Data parameter holds its argument as written.  Comparing the held term
; with == reads it as syntax: nothing inside it runs, whether or not it
; contains variables, and the answer is about the term as written.
(= (exists $d $p) (println! ran-exists))
(: is-nil (-> Atom Bool))
(= (is-nil $t) (== $t ()))
!(is-nil (exists P (lam y (P y))))
!(is-nil (exists P (lam $y (P $y))))
!(is-nil ())

; The same under if, and between two held terms.
(: is-nil-if (-> Atom Bool))
(= (is-nil-if $t) (if (== $t ()) True False))
!(is-nil-if (exists P (lam $y (P $y))))
(: same (-> Atom Atom Bool))
(= (same $a $b) (== $a $b))
!(same (exists P (lam $y (P $y))) (exists P (lam $y (P $y))))
!(same (exists P (lam $y (P $y))) (exists Q (lam $y (Q $y))))

; Holding is an internal carrier, not part of the observed syntax.  Structural
; observers erase it at every depth, without activating the syntax it holds.
(: hold (-> Atom Atom))
(= (hold $t) $t)
!(== (Box (hold 3)) (Box 3))
!(== (Box (Box (hold 3))) (Box (Box 3)))
!(=alpha (hold 3) 3)
!(=alpha (Box (hold 3)) (Box 3))
!(=alpha (Box (Box (hold 3))) (Box (Box 3)))
!(unique-atom ((Box (hold 3)) (Box 3)))
!(subtraction-atom ((Box (hold 3)) kept) ((Box 3)))
!(== (Box (hold (+ 1 2))) (Box (hold (+ 1 2))))
!(== (Box (hold (+ 1 2))) (Box 3))
!(== (Box (hold (exists P (lam $y (P $y)))))
     (Box (hold (exists P (lam $y (P $y))))))

; The held term is returned as written, and runs only when evaluated.
!(hold (exists P (lam $y (P $y))))
!(let $d (hold (exists P (lam $y (P $y)))) (eval $d))
