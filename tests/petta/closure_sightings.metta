; A relation that only applies a closure runs as written for a value's first
; call and is specialized for the value from its second; one that passes the
; closure on is specialized at once.  Answers are the same either way.
(= (add-k $k $x) (+ $k $x))
(= (use $f $x) ($f $x))
!(+ (use (add-k 1) 1) (+ (use (add-k 2) 1) (+ (use (add-k 1) 1) (use (add-k 2) 1))))
; More values of one shape than the sightings kept, then the first again.
(= (sum-over $l $acc)
   (if (== $l ()) $acc
       (sum-over (cdr-atom $l) (+ $acc (use (add-k (car-atom $l)) 0)))))
!(sum-over (0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69) 0)
!(use (add-k 0) 5)
!(use (add-k 0) 6)
; Applied twice inside data, and to each of several choices.
(= (both $f $x $y) (Pair ($f $x) ($f $y)))
!(both (add-k 3) 1 2)
!(both (add-k 3) 10 20)
(= (each $f) ($f (superpose (1 2 3))))
!(collapse (each (add-k 10)))
!(collapse (each (add-k 10)))
; Passed on: around a recursion, into a fold's body, beside a closure that
; is only applied.
(= (map-cells $f ()) ())
(= (map-cells $f (cons $x $xs)) (cons ($f $x) (map-cells $f $xs)))
!(map-cells (add-k 1) (cons 1 (cons 2 (cons 3 ()))))
(= (sum-with $f $l) (foldl-atom $l 0 $acc $x (+ $acc ($f $x))))
!(sum-with (add-k 1) (1 2 3 4))
!(sum-with (add-k 2) (1 2 3 4))
(= (go $f $g $n $acc) (if (== $n 0) ($g $acc) (go $f $g (- $n 1) ($f $acc))))
!(go (add-k 1) (add-k 100) 5 0)
!(go (add-k 2) (add-k 100) 5 0)
; One equation applies the closure, another passes it on.
(= (walk $f (One $x)) ($f $x))
(= (walk $f (Many $x $xs)) (+ ($f $x) (walk $f $xs)))
!(walk (add-k 1) (One 5))
!(walk (add-k 2) (Many 1 (Many 2 (One 3))))
; Redefined between the first and the second call.
(= (pick $f $x) ($f $x))
!(pick (add-k 50) 1)
!(add-atom &self (= (pick $f $x) (Picked ($f $x))))
!(collapse (pick (add-k 50) 1))
; Captured data that looks like a call stays data.
(Stored (inc 5))
(= (inc $x) (+ $x 1))
(= (keep $d $x) (Kept $d $x))
!(collapse (match &self (Stored $d) (use (keep $d) 1)))
!(collapse (match &self (Stored $d) (use (keep $d) 2)))
