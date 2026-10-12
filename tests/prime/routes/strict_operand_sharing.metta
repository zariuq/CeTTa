; A pending field is computed once in its branch, however many strict
; operands read it.  Here the value is a data tuple whose head was computed
; by a call, passed as an argument; a primitive reads it in both operands,
; and each field's effect runs once.

(= (noisy $k) (let $u (println! (forced $k)) $k))
(= (route $k) (Route b (0) (noisy $k)))
(= (eqself $r) (== $r $r))
!(eqself ((route 1) (route 2)))
!(eqself ((route 3) 5))
; The same value read twice through a `let` is computed once as well.
!(let $r ((route 4) 5) (Pair (== $r $r) (== $r $r)))
