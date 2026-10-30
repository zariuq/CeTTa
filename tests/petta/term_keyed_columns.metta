; A column holding a nested constant, here a level spelled (S (S ... Z)),
; selects its rows by that constant's term, as deep as the levels go; the
; rows of a level, their counts and the joins from a level answer as the
; facts say.

!(bind! &t (new-space set))

(= (level $n) (if (> $n 0) (S (level (- $n 1))) Z))

(= (fill $k $n)
   (if (>= $k $n)
       ()
       (let* (($l (level $k))
              ($j (+ $k 1))
              ($a (add-atom &t (rung $l a)))
              ($b (add-atom &t (rung $l b)))
              ($c (add-atom &t (next $k $j))))
         (fill (+ $k 1) $n))))

!(fill 0 14)

; A deep level selects its own rows only.
!(collapse (match &t (rung (S (S (S (S (S (S (S (S (S (S (S Z))))))))))) $x) $x))
!(collapse (match &t (rung (S (S Z)) $x) $x))
!(collapse (match &t (rung Z $x) $x))
!(collapse (match &t (rung (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S Z))))))))))))))) $x) $x))

; Counts of a level, shallow and deep, and of every level.
!(size-atom (collapse (match &t (rung (S (S (S (S (S (S (S (S (S (S Z)))))))))) $x) t)))
!(size-atom (collapse (match &t (rung Z $x) t)))
!(size-atom (collapse (match &t (rung $l $x) t)))
!(size-atom (collapse (match &t (rung $l a) t)))

; A join from a bound level, and one through every level.
!(collapse (match &t (, (rung (S (S (S (S (S (S (S (S (S Z))))))))) $x) (next 9 $k)) ($x $k)))
!(size-atom (collapse (match &t (, (rung $l $x) (next $k $j)) t)))

; A variable before a level: the level still selects its own rows.
(= (hold $k $n)
   (if (>= $k $n)
       ()
       (let* (($l (level $k))
              ($a (add-atom &t (held a $l)))
              ($b (add-atom &t (held b $l))))
         (hold (+ $k 1) $n))))

!(hold 0 14)
!(collapse (match &t (held $x (S (S (S (S (S (S (S (S (S (S (S Z)))))))))))) $x))
!(collapse (match &t (held $x Z) $x))
!(size-atom (collapse (match &t (held $x (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S Z)))))))))))))))) t)))
!(size-atom (collapse (match &t (, (held $x $l) (rung $l $x)) t)))

; The levels in a stack, which reads its rows in turn: each level compares
; equal to itself alone, however deep.
!(bind! &s (new-space stack))

(= (stack-levels $k $n)
   (if (>= $k $n)
       ()
       (let* (($l (level $k))
              ($a (add-atom &s (val $k $l))))
         (stack-levels (+ $k 1) $n))))

!(stack-levels 0 14)
!(collapse (match &s (val $k (S (S (S (S (S (S (S (S (S (S (S Z)))))))))))) $k))
!(collapse (match &s (val $k Z) $k))
!(size-atom (collapse (match &s (val $k (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S Z)))))))))))))))) t)))
!(size-atom (collapse (match &s (val $k $l) (match &s (val $j $l) t))))
