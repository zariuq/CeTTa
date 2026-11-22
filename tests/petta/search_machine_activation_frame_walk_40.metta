; The walk of search_machine_activation_frame_walk.metta over 40 cells: its
; binding reads and activations grow linearly with the walk's length.
(= (inner-empty $p) (let $c (empty) $p))
(= (count-cells ()) 0)
(= (count-cells (cons $p $rest)) (let $u (collapse (inner-empty $p)) (+ 1 (count-cells $rest))))
!(count-cells (1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40))
