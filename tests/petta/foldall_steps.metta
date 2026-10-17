; foldall runs its generator and, on each answer in turn, its step, both as
; PeTTa evaluations: a step's declaration governs its application, an error
; the step raises leaves the state as it was, and the generator's and the
; step's effects interleave as they come.

(: ns (-> Number Number Number))
(= (ns $x $y) $x)
(: cc-sum (-> Number Number Number))
(= (cc-sum $x $y) (+ $x $y))
(= (u-sum $x $y) (+ $x $y))

; A typed step: its guards refuse the wrong item, so the state stays.
!(collapse (foldall ns (superpose (1 wrong)) 0))
!(collapse (foldall (|-> ($i $a) (ns $i $a)) (superpose (1 wrong)) 0))
!(collapse (foldall cc-sum (superpose (1 wrong)) 0))
; An untyped step that raises: reduce/2 takes the error, the state stays.
!(collapse (foldall u-sum (superpose (1 wrong)) 0))
!(collapse (foldall u-sum (superpose (1 2 3)) 10))

; Effects interleave: each generator answer, then the step on it.
(= (gen) (superpose ((progn (println! g1) 1) (progn (println! g2) 2))))
(= (st $v $s) (progn (println! (step $v $s)) (+ $v $s)))
!(foldall st (gen) 0)

; Each answer of the step is the state in turn; the last one stays.
(= (two $v $s) (superpose ((+ $v $s) (* 10 (+ $v $s)))))
!(foldall two (superpose (1 2)) 0)

; A step computed from an expression, applied to values that stay values.
(= (adder) (|-> ($v $s) (Pair $v $s)))
!(foldall (adder) (superpose ((foo bar) (baz))) init)

; The state is a copy that takes the goals delayed on its variables along,
; as a collection's copies do: they hold when the fold's value is bound,
; and a step that refuses its constraint leaves the state as it was.
(= (bounded $v $s) (let $t (#+ $s $v) (let $_ (translatePredicate (#< $t 5)) $t)))
!(collapse (let* (($f (foldall bounded (superpose (1 2)) $z)) ($f 4)) ok))
!(collapse (let* (($f (foldall bounded (superpose (1 2)) $z)) ($f 6)) ok))
!(foldall bounded (superpose (1 2)) 1)
!(collapse (foldall bounded (superpose (1 2)) 3))
