; A relation in the finite-answer fragment, consumed one answer at a time
; while its continuation changes the program.  Each call reads the equations
; present when it is made; data-only changes leave every call's view intact.

(= (pk1 (Cons $x $xs)) $x)
(= (pk1 (Cons $x (Cons $y $ys))) (pk1 (Cons $y $ys)))
!(let $c (pk1 (Cons 1 (Cons 2 (Cons 3 Nil))))
   (let $u (if (== $c 1) (add-atom &self (= (pk1 (Cons $a $b)) 100)) ())
     $c))

(= (pk2 (Cons $x $xs)) (v $x))
(= (pk2 (Cons $x (Cons $y $ys))) (pk2 (Cons $y $ys)))
!(let $c (pk2 (Cons 1 (Cons 2 (Cons 3 (Cons 4 Nil)))))
   (let $u (if (== $c (v 1)) (remove-atom &self (= (pk2 (Cons $a (Cons $b $r))) (pk2 (Cons $b $r)))) ())
     $c))

(= (pk3 (Cons $x $xs)) $x)
(= (pk3 (Cons $x (Cons $y $ys))) (pk3 (Cons $y $ys)))
!(let $c (pk3 (Cons 1 (Cons 2 (Cons 3 Nil))))
   (let $u (add-atom &self (seen $c))
     $c))
!(collapse (match &self (seen $s) $s))

(= (g4 (S $n)) (g4 $n))
(= (g4 $n) $n)
!(let $c (g4 (S (S (S Z))))
   (let $u (if (== $c Z) (add-atom &self (= (g4 $m) (tag $m))) ())
     $c))
!(g4 (S Z))

(= (g5 $n) $n)
(= (g5 (S $n)) (g5 $n))
!(let $c (g5 (S (S Z)))
   (let $u (if (== $c (S (S Z))) (add-atom &self (= (g5 $m) (tag $m))) ())
     $c))

(= (color 1) red)
(= (color 2) green)
(= (color $n) (shade $n))
(= (shade 1) dark)
(= (shade $n) light)
!(collapse (color 1))
!(let $c (color 1) (let $u (if (== $c red) (add-atom &self (= (shade $k) pale)) ()) $c))

(= (hue 1) red)
(= (hue $n) (tone $n))
(= (tone 1) dark)
(= (tone $n) light)
!(let $c (hue 1) (let $u (if (== $c red) (remove-atom &self (= (tone 1) dark)) ()) $c))
!(collapse (hue 1))

(= (pk8 (Cons $x $xs)) $x)
(= (pk8 (Cons $x (Cons $y $ys))) (pk8 (Cons $y $ys)))
!(let $a (pk8 (Cons 1 (Cons 2 Nil))) (let $b (pk8 (Cons 3 (Cons 4 (Cons 5 Nil)))) (P $a $b)))
!(once (pk8 (Cons 7 (Cons 8 Nil))))
!(collapse (pk8 (Cons 1 (Cons 2 (Cons 3 Nil)))))
!(pk8 Nil)
!(let $c (pk8 (Cons 1 (Cons 2 (Cons 3 Nil)))) (if (> $c 1) (empty) $c))

(= (mk $n $acc) (if (== $n 0) $acc (let $e (superpose ($n)) (mk (- $n 1) (Cons $e $acc)))))
(= (len Nil) 0)
(= (len (Cons $h $t)) (+ 1 (len $t)))
!(let $c (pk8 (Cons 10 (Cons 20 (Cons 30 Nil)))) (let $l (mk 40000 Nil) (P $c (len $l))))

; List-form heads: cons values are enumerated by the producer; a flat list is
; a PeTTa list too, which only equation search can split, so the producer
; hands such a call back before its first answer.
(= (pkl (cons $x $xs)) $x)
(= (pkl (cons $x (cons $y $ys))) (pkl (cons $y $ys)))
!(collapse (pkl (cons 1 (cons 2 (cons 3 ())))))
!(collapse (pkl (1 2 3)))
!(let $c (pkl (cons 1 (cons 2 (cons 3 ()))))
   (let $u (if (== $c 1) (add-atom &self (= (pkl (cons $a $b)) 100)) ())
     $c))
(= (sel (cons $x $xs)) (P $x $xs))
(= (sel (cons $x $xs)) (sel $xs))
!(collapse (sel (cons a (cons b ()))))
!(collapse (sel (a b)))
