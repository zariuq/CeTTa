; PeTTa's car-atom and cdr-atom are total: anything but a non-empty
; expression gives (), an ordinary value.  Each result is collected, so a
; missing answer shows as () rather than (()).
!(test (collapse (car-atom ())) (()))
!(test (collapse (cdr-atom ())) (()))
!(test (collapse (car-atom 5)) (()))
!(test (collapse (cdr-atom foo)) (()))
!(test (chain (car-atom ()) $ignored survived) survived)

; Positive contracts remain unchanged.
!(test (collapse (car-atom (a b))) (a))
!(test (collapse (cdr-atom (a b))) ((b)))

; A walk that reaches the end of its list continues with ().
(= (hd $l) (car-atom $l))
(= (walk $n $l) (if (> $n 0) (walk (- $n 1) $l) (hd $l)))
!(test (collapse (walk 2 ())) (()))
!(test (collapse (walk 2 5)) (()))
!(test (collapse (walk 2 (a b))) (a))
