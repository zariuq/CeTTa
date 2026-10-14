; A match over a conjunction reads its rows whole before any template runs,
; so a template's effects reach neither the rows of the leg it runs under
; nor the rows of a later leg.  The compiled machine completes the joint
; rows natively; rows with variables of their own, and rows that would
; evaluate further, are completed by the host.  Each case is one call whose
; body has effects.

; A template adds a row that a later leg would read.
(edge a b)
(edge b c)
(= (grow)
   (match &self (, (edge $x $y) (edge $y $z))
      (let $u (add-atom &self (edge $z d)) (path $x $y $z))))
!(grow)
!(collapse (match &self (edge $p d) $p))

; A template removes a row that a later joint row reads.
(item 1 one)
(item 2 two)
(pair 1 2)
(pair 2 1)
(= (take)
   (match &self (, (pair $i $j) (item $j $n))
      (let $u (remove-atom &self (item $j $n)) (took $i $n))))
!(take)
!(collapse (match &self (item $k $v) $k))

; Joint rows keep their multiplicity, and three legs complete together.
(t x)
(t x)
(u x 1)
(u x 2)
(w 1 p)
(w 2 q)
(= (three)
   (match &self (, (t $a) (u $a $b) (w $b $c))
      (let $u (add-atom &self (seen $c)) $c)))
!(three)
!(collapse (match &self (seen $s) $s))

; A row with a variable of its own: the host completes the rows.
(open k $v)
(open k2 5)
(key k)
(key k2)
(= (opened)
   (match &self (, (key $k) (open $k $w))
      (let $u (add-atom &self (got $k)) (opened $k))))
!(opened)
!(collapse (match &self (got $g) $g))

; A row part that would evaluate further: the host completes the rows.
(= (double $n) (* 2 $n))
(calc a (double 3))
(calc b 4)
(name a)
(name b)
(= (calcs)
   (match &self (, (name $m) (calc $m $e))
      (let $u (add-atom &self (did $m)) $e)))
!(calcs)
!(collapse (match &self (did $d) $d))
