; Prepared programs match equation heads without equation search.  They must
; decline, never guess, where PeTTa's own matching differs from a structural
; match.

; An equation head that holds a call is relational: the call is evaluated and
; its value unified with the argument, even when the argument is data of the
; call's shape.
(= (g $y) (S $y))
(= (f (g $x)) yes)
!(let $v (quote (g 5)) (f $v))
!(f (g 5))
!(f (S 5))

(= (mark $label) (progn (println! $label) fixed))
(= (pair (mark first) (mark second)) accepted)
(= (pair $a $b) fallback)
!(collapse (pair fixed fixed))

; A list-form head matches every non-empty expression, whose tail is no
; existing atom.
(= (hd (cons $x $xs)) $x)
(= (hd $other) none)
!(hd (a b c))
!(collapse (hd (a b c)))
!(hd (cons 1 (cons 2 ())))
!(hd ())
!(hd atom)

; Pre-interning keys makes the stored clause order differ from key order.
(remember-key k7)
(remember-key k0)
(= (point k0) 0)
(= (point k1) 1)
(= (point k2) 2)
(= (point k3) 3)
(= (point k4) 4)
(= (point k5) 5)
(= (point k6) 6)
(= (point k7) 7)
(= (indexed-sum $x $y) (+ (point $x) (point $y)))
!(indexed-sum k0 k7)
!(collapse (point missing))
!(add-atom &self (= (point k7) duplicate))
!(collapse (point k7))
!(add-atom &self (= (point $key) wildcard))
!(collapse (point missing))
