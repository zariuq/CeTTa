; cut: the equations after the one that cuts are not tried, and neither are
; the choices made earlier in its body.
(= (committed) (case (progn (cut) a) ((a A) (Empty DEFAULT))))
(= (committed) OUTSIDE)
!(test (collapse (committed)) (A))

(= (pick 1) (progn (cut) one))
(= (pick $n) (other $n))
!(test (collapse (pick 1)) (one))
!(test (collapse (pick 2)) ((other 2)))
!(test (collapse (pick 1)) (one))

(= (first-of) (let $x (superpose (1 2 3)) (progn (cut) $x)))
(= (first-of) 99)
!(test (collapse (first-of)) (1))

(= (late) before)
(= (late) (progn (cut) middle))
(= (late) after)
!(test (collapse (late)) (before middle))
