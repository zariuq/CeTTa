; A fresh activation frame elides occurs checks at its own slots only while
; no stored value mentions the frame.  Each self-application below writes
; the caller variable to a term over the frame first and must then refuse
; the cyclic binding, including one reached through that caller variable.
; A caller variable written to a ground value leaves the elision in place.
(= (fresh-f (fresh-h $x) $x) ok)
(= (fresh-g $y (fresh-h $y)) ok)
(= (fresh-n (fresh-h $x) $x) ok)
(= (fresh-k leaf $x) (seen $x))
(= (grow-pair 0 $t) (fresh-f (fresh-h $t) $t))
(= (grow-pair $n $t) (if (> $n 0) (grow-pair (- $n 1) (node $n $t)) (empty)))
!(fresh-f (fresh-h leaf) leaf)
!(fresh-f $q $q)
!(fresh-g $r $r)
!(fresh-f (fresh-h $z) $z)
!(fresh-n $r (fresh-m $r))
!(fresh-k $q $q)
!(collapse (grow-pair 6 leaf))
!(let $w (fresh-h $w) cyclic)
