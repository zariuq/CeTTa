; In PeTTa `Empty` is an ordinary symbol, except as the default branch of
; `case`, which runs when the scrutinee has no result.  No result itself is
; written (empty).
!(test (collapse (let $x Empty $x)) (Empty))
!(test (collapse (Empty)) ((Empty)))
!(test (collapse (superpose (a Empty b))) (a Empty b))
(= (f) Empty)
!(test (collapse (f)) (Empty))
!(test (collapse (car-atom (Empty x))) (Empty))
!(test (== Empty Empty) true)

; The case default and (empty) keep their meanings.
!(test (case (empty) ((1 2) (Empty 42))) 42)
!(test (case 3 ((3 ok) (Empty nok))) ok)
!(test (case Empty ((Empty yes) ($x no))) no)
!(test (collapse (empty)) ())
!(test (collapse (if False yes)) ())
!(test (collapse (index-atom (a b) 5)) ())
