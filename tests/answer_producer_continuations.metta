; Relations whose bodies resume after a call: each answer of the call
; continues the body from the same state.  Answers keep equation order and
; multiplicity.  A call that matches no equation fails in PeTTa and stands for
; itself in HE.  A repeated head variable matches equal values only.

(= (choose (Cons $x $xs)) (Pair $x $xs))
(= (choose (Cons $x $xs)) (let (Pair $y $ys) (choose $xs) (Pair $y (Cons $x $ys))))
(= (perm Nil) Nil)
(= (perm (Cons $h $t)) (let (Pair $y $ys) (choose (Cons $h $t)) (Cons $y (perm $ys))))
!(perm (Cons 1 (Cons 2 (Cons 3 Nil))))

(= (sublist Nil) Nil)
(= (sublist (Cons $x $xs)) (Cons $x (sublist $xs)))
(= (sublist (Cons $x $xs)) (sublist $xs))
!(sublist (Cons a (Cons b (Cons c Nil))))

(= (between $lo $hi) (if (> $lo $hi) (empty) $lo))
(= (between $lo $hi) (if (< $lo $hi) (between (+ $lo 1) $hi) (empty)))
(= (triple $n)
   (let $a (between 1 $n)
     (let $b (between $a $n)
       (let $c (between $b $n)
         (if (== (+ (* $a $a) (* $b $b)) (* $c $c)) (Triple $a $b $c) (empty))))))
!(triple 13)

(= (tree 0) Leaf)
(= (tree $n) (if (> $n 0) (let $k (between 0 (- $n 1)) (Node (tree $k) (tree (- (- $n 1) $k)))) (empty)))
!(tree 3)

(= (perhaps) (Some 1))
(= (perhaps) None)
(= (perhaps) (Some 2))
(= (perhaps) (Some 1))
(= (somes) (let (Some $x) (perhaps) (Got $x)))
!(somes)

(= (coin) heads)
(= (coin) tails)
(= (coin) heads)
(= (toss) (Toss (coin) (coin)))
!(toss)

(= (gen) 1)
(= (gen) 2)
(= (bad) (Cons (gen) (empty)))
!(bad)

(= (hue) red)
(= (hue) green)
(= (hue) blue)
(= (coloring)
   (let $a (hue)
     (let $b (hue)
       (if (== $a $b) (empty)
         (let $c (hue)
           (if (== $a $c) (empty) (if (== $b $c) (empty) (Map $a $b $c))))))))
!(coloring)

(= (edge a) b)
(= (edge a) c)
(= (edge b) c)
(= (reach $to $to) (Cons $to Nil))
(= (reach $from $to) (if (== $from $to) (empty) (let $next (edge $from) (Cons $from (reach $next $to)))))
!(reach a c)

(= (pick (Cons $x $xs)) $x)
(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))
(= (upto $n) (if (== $n 0) Nil (Cons $n (upto (- $n 1)))))
(= (safe $c $placed $dist)
   (if (== $placed Nil) True
       (let (Cons $h $t) $placed
         (if (== $c $h) False
             (if (== (- $c $h) $dist) False
                 (if (== (- $h $c) $dist) False
                     (safe $c $t (+ $dist 1))))))))
(= (place $n $cols $placed)
   (if (== $n 0) $placed
       (let $c (pick $cols)
         (if (safe $c $placed 1)
             (place (- $n 1) $cols (Cons $c $placed))
             (empty)))))
!(place 5 (upto 5) Nil)

(= (unmatched a) b)
(= (unmatched a) c)
(= (wrap $x) (Box (unmatched $x)))
!(wrap z)
