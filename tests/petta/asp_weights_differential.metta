; Seeded programs with weight statements: choices, rules, positive loops,
; constraints, weight rules, weight constraints and weight choices over a few
; atoms, a weight's head sometimes among its own body's positive atoms.  Each
; line is a program's stable models, each sorted, or `refused`.  The solver
; propagates weight statements itself where no positive cycle runs through
; them; with CETTA_ASP_WEIGHTS_NORMALIZED=1 every one is read by the verified
; sequential counter instead, and the lines must be the same.
!(import! &self asp)

(= (next $s) (% (* $s 48271) 2147483647))
(= (pick $s $xs) (index-atom $xs (% $s (size-atom $xs))))

(= (atoms) (a b c d e))
(= (weights) (1 1 1 2 3))
(= (bounds) (0 1 1 2 2 3 4 6))

(= (statement $s)
   (let* (($s1 (next $s)) ($s2 (next $s1)) ($s3 (next $s2)) ($s4 (next $s3))
          ($s5 (next $s4)) ($s6 (next $s5)) ($s7 (next $s6)) ($s8 (next $s7))
          ($x (pick $s1 (atoms))) ($y (pick $s2 (atoms))) ($z (pick $s3 (atoms)))
          ($u (pick $s4 (atoms)))
          ($w1 (pick $s5 (weights))) ($w2 (pick $s6 (weights))) ($w3 (pick $s7 (weights)))
          ($l (pick $s8 (bounds)))
          ($k (% (next $s8) 10)))
     (if (< $k 3) (choice $x () ())
     (if (== $k 3) (rule $x ($y) ($z))
     (if (== $k 4) (rule $x ($y) ())
     (if (== $k 5) (weight-rule $x $l (($y $w1) ($z $w2)) (($u $w3)))
     (if (== $k 6) (weight-rule $x $l (($x $w1) ($y $w2)) ())
     (if (== $k 7) (weight-constraint $l (($x $w1) ($y $w2)) (($z $w3)))
     (if (== $k 8) (weight-choice ($x $y) $l (($z $w1)) (($u $w2)))
                   (constraint ($x) ($y)))))))))))

(= (program $s $n)
   (if (== $n 0) ()
     (cons-atom (statement $s) (program (next (next (next $s))) (- $n 1)))))

(= (canon-each $ms) (if (== $ms ()) () (let ($h $t) (decons-atom $ms) (cons-atom (sort $h) (canon-each $t)))))
(= (canon $ms) (sort (canon-each $ms)))

(= (models-of $p)
   (let $r (asp:models $p)
     (if (== (car-atom $r) models)
         (canon (car-atom (cdr-atom $r)))
         refused)))

(= (scenario $seed)
   (let* (($n (+ 4 (% $seed 6)))
          ($p (program (next $seed) $n)))
     ($seed (models-of $p))))

(= (scenarios $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (scenario $seed))
       (scenarios (next (next (next (next $seed)))) (- $n 1)))))

!(scenarios 7 200)
