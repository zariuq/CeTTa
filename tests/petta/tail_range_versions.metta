; A version whose calls to itself are all in tail positions needs no proof
; of its closed range: each such call's answers are its caller's, which the
; caller's own result check meets once.  The entry keeps that check.
(: cnt (-> Number %Undefined% Number))
(= (cnt $n $acc) (if (== $n 0) $acc (cnt (- $n 1) (+ $acc 1))))
!(cnt 50 0)
!(collapse (cnt 0 "s"))
(: cs (-> Number %Undefined% Number))
(= (cs $n $acc) (if (== $n 0) "s" (cs (- $n 1) $acc)))
!(collapse (cs 3 0))
(: lt (-> Number %Undefined% Number))
(= (lt $n $acc) (if (== $n 0) $acc (let $m (- $n 1) (lt $m (+ $acc 2)))))
!(lt 10 0)
; A call to itself whose answer is used, not only answered, keeps the check
; of every call.
(: nt (-> Number %Undefined% Number))
(= (nt $n $acc) (if (== $n 0) $acc (+ 1 (nt (- $n 1) $acc))))
!(nt 5 0)
!(collapse (nt 0 "s"))
(: pick (-> Number %Undefined% Number))
(= (pick $n $acc) (if (== $n 0) $acc (superpose ((pick (- $n 1) $acc) (pick (- $n 1) 7)))))
!(sort-atom (collapse (pick 2 1)))
!(collapse (pick 0 "x"))
; So across equations: one whose answer is not shown of the range and another
; whose call to itself is used keep the check of every call.
(: ml (-> Number Number))
(= (ml 0) "z")
(= (ml $n) (if (> $n 0) (+ 1 (ml (- $n 1))) (empty)))
!(collapse (ml 2))
