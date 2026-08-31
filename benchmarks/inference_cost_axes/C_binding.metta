(= (extract (→ (→ $a (→ $b $c)) (→ (→ $d $e) (→ $f $g)))) ($a $b $c $d $e $f $g))
(= (drive $n $acc) (if (== $n 0) $acc (let $r (extract (→ (→ p (→ q r)) (→ (→ s t) (→ u v)))) (drive (- $n 1) (+ $acc 1)))))
(= (drive $n $acc) (if (< $n 0) never (empty)))
!(drive 150000 0)
