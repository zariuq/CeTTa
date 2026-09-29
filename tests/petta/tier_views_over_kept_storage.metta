; A list walked by a (cons $x $xs) head is a view of the list's storage at
; each step.  A view over storage a collection keeps where it is (the host's,
; or the old generation's in a minor collection) is traced and copied as its
; header alone, so walking a long list stays linear in its length.  Every
; answer is SWI-PeTTa's.
(= (range $a $b) (if (< $a $b) (cons-atom $a (range (+ $a 1) $b)) ()))
(= (wrap-all ()) ())
(= (wrap-all (cons $x $xs)) (cons-atom (w $x) (wrap-all $xs)))
(= (count-down $n) (if (> $n 0) (superpose ($n (count-down (- $n 1)))) (empty)))
!(size-atom (wrap-all (range 0 100000)))
!(let $l (collapse (count-down 3000)) (size-atom (wrap-all $l)))
!(let $l (collapse (count-down 3000)) (car-atom (wrap-all $l)))
!(wrap-all (1 (2 3) "four"))
