; A count of a match under each solution of another, bound by a let and
; added with the solution: the counts taken in one pass, grouped by the
; values the counted pattern shares with the solution.
(pair a b)
(pair a c)
(pair b c)
(pair d e)
(mf a b x)
(mf a b y)
(mf a c x)
(mf b c x)
(mf b c y)
(mf b c z)
!(bind! &r (new-space set))
!(size-atom (collapse (match &self (pair $p $q) (let $n (size-atom (collapse (match &self (mf $p $q $x) t))) (add-atom &r (rec $p $q $n))))))
!(sort-atom (collapse (match &r (rec $p $q $n) ($p $q $n))))
; The count through a function of one equation, and a count no variable of
; the solution keys.
(= (count-of $p) (size-atom (collapse (match &self (mf $p $q $x) t))))
!(bind! &s (new-space set))
!(size-atom (collapse (match &self (pair $p $q) (let $n (count-of $p) (add-atom &s (per $p $n))))))
!(sort-atom (collapse (match &s (per $p $n) ($p $n))))
!(bind! &u (new-space set))
!(size-atom (collapse (match &self (pair $p $q) (let $n (size-atom (collapse (match &self (mf $a $b $c) t))) (add-atom &u (all $p $q $n))))))
!(sort-atom (collapse (match &u (all $p $q $n) ($p $q $n))))
; Additions into the counted space under another head, and none at all
; (no solution).
!(bind! &h (new-space set))
!(add-atom &h (mc 1 a))
!(add-atom &h (mc 1 b))
!(add-atom &h (mc 2 c))
!(add-atom &h (bucket 1))
!(add-atom &h (bucket 2))
!(add-atom &h (bucket 3))
!(size-atom (collapse (match &h (bucket $n) (let $c (size-atom (collapse (match &h (mc $n $p) t))) (add-atom &h (hist $n $c))))))
!(sort-atom (collapse (match &h (hist $n $c) ($n $c))))
!(size-atom (collapse (match &self (pair $p zz) (let $n (size-atom (collapse (match &self (mf $p $q $x) t))) (add-atom &r (none $p $n))))))
; An addition the count could read: each solution sees what the ones
; before it added, which the ordinary path keeps.
!(bind! &g (new-space set))
!(add-atom &g (k 1))
!(add-atom &g (k 2))
!(add-atom &g (k 3))
!(size-atom (collapse (match &g (k $i) (let $n (size-atom (collapse (match &g (k $j) t))) (let $_ (add-atom &g (k (x $i))) (add-atom &g (cnt $i $n)))))))
!(sort-atom (collapse (match &g (cnt $i $n) ($i $n))))
; Solutions repeated in a bag: each is counted and added for.
!(bind! &b (new-space))
!(add-atom &b (row a))
!(add-atom &b (row a))
!(add-atom &b (row b))
!(bind! &c (new-space))
!(size-atom (collapse (match &b (row $p) (let $n (size-atom (collapse (match &self (mf $p $q $x) t))) (add-atom &c (got $p $n))))))
!(sort-atom (collapse (match &c (got $p $n) ($p $n))))
