; A stored term 200,000 levels deep: stored, read back by a match, counted,
; compared with a fresh copy and matched below its top.
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (s $acc))))
(= (peel $t $k) (if (== $t z) $k (case $t (((s $u) (peel $u (+ $k 1)))))))
!(let $t (build 200000 z) (add-atom &self (d $t)))
!(match &self (d $x) (peel $x 0))
!(let $t (build 200000 z) (match &self (d $x) (== $x $t)))
!(match &self (d (s (s $x))) (peel $x 2))
