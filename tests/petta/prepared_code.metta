; Closed pure calls compiled to register code: every instruction kind,
; each call entry, and the paths that leave a call to the evaluator.

; Keyed recursion with a constructor around the call (keyed entry).
(= (app Nil $ys) $ys)
(= (app (Cons $x $xs) $ys) (Cons $x (app $xs $ys)))
(= (rev Nil) Nil)
(= (rev (Cons $x $xs)) (app (rev $xs) (Cons $x Nil)))
(= (upto $n) (if (== $n 0) Nil (Cons $n (upto (- $n 1)))))
!(rev (upto 6))

; Arithmetic recursion: branches on comparisons (variables entry).
(= (fib $n) (if (< $n 2) $n (+ (fib (- $n 1)) (fib (- $n 2)))))
!(fib 15)

; Keys on two argument positions with nested patterns.
(= (pack Z $n) (S $n))
(= (pack (S $m) Z) (pack $m (S Z)))
(= (pack (S $m) (S $n)) (pack $m (pack (S $m) $n)))
(= (p2i Z) 0)
(= (p2i (S $x)) (+ 1 (p2i $x)))
(= (i2p $n) (if (== $n 0) Z (S (i2p (- $n 1)))))
!(p2i (pack (i2p 2) (i2p 3)))

; A let pattern, and a let whose pattern fails.
(= (mkpair $a $b) (Pair $a $b))
(= (sumpair $p) (let (Pair $a $b) $p (+ $a $b)))
!(sumpair (mkpair 3 4))
!(sumpair (Single 3))

; A long loop of tail calls.
(= (count $n $acc) (if (== $n 0) $acc (count (- $n 1) (+ $acc 1))))
!(count 200000 0)

; A condition that is not a truth value.
(= (pick $c) (if $c yes no))
!(pick 7)

; Many arguments and a wide constructor.
(= (sum17 $a $b $c $d $e $f $g $h $i $j $k $l $m $n $o $p $q)
   (+ $a (+ $b (+ $c (+ $d (+ $e (+ $f (+ $g (+ $h (+ $i (+ $j
     (+ $k (+ $l (+ $m (+ $n (+ $o (+ $p $q)))))))))))))))))
!(sum17 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17)
(= (wide $x) (W $x $x $x $x $x $x $x $x $x $x $x $x $x $x $x $x $x $x))
!(wide 1)

; A call with no arguments.
(= (five) 5)
(= (plus-five $x) (+ (five) $x))
!(plus-five 1)

; Overlapping equations: more than one answer.
(= (either $x) (L $x))
(= (either $x) (R $x))
!(either 1)

; A large structure live across collections while more is built.
(= (build $n) (if (== $n 0) Nil (Cons (Box $n) (build (- $n 1)))))
(= (total Nil) 0)
(= (total (Cons (Box $v) $t)) (+ $v (total $t)))
(= (twice $n) (+ (total (build $n)) (total (build $n))))
!(twice 50000)

; An answer producer: its search runs as steps, and a step whose region
; cannot choose runs as register code while the search still reads the
; step's locals.
(= (choose (Cons $x $xs)) $x)
(= (choose (Cons $x (Cons $y $ys))) (choose (Cons $y $ys)))
(= (fits $c $placed)
   (if (== $placed Nil) True
       (let (Cons $h $t) $placed (if (== $c $h) False (fits $c $t)))))
(= (distinct $n $cols $placed)
   (if (== $n 0) $placed
       (let $c (choose $cols)
         (if (fits $c $placed) (distinct (- $n 1) $cols (Cons $c $placed))
             (empty)))))
!(distinct 2 (upto 3) Nil)
