; Ground matches over a space whose other rows hold variables: a row with
; variables under the pattern's head can match a ground pattern without
; being it; duplicates keep their multiplicity.  The pad rows keep the
; space past the size below which every row is a candidate.
(pad 1)
(pad 2)
(pad 3)
(pad 4)
(pad 5)
(pad 6)
(pad 7)
(pad 8)
(pad 9)
(pad 10)
(pad 11)
(pad 12)
(pad 13)
(pad 14)
(pad 15)
(pad 16)
(pad 17)
(pad 18)
(pad 19)
(pad 20)
(fact a 1)
(fact a 1)
(fact b 2)
(rule $x 3)
(deep (s (s (s (s (s (s (s (s (s (s z)))))))))))
(deep (s (s (s (s (s (s (s (s (s (s (s z))))))))))))

(= (exists $p) (if (== () (collapse (once (match &self $p $p)))) absent present))

!(collapse (match &self (fact a 1) yes))
!(collapse (match &self (fact b 2) yes))
!(collapse (match &self (fact c 3) yes))
!(exists (fact a 1))
!(exists (fact c 3))
!(collapse (match &self (rule z 3) yes))
!(exists (rule z 3))
!(collapse (match &self (deep (s (s (s (s (s (s (s (s (s (s z))))))))))) yes))
!(collapse (match &self (deep (s (s (s (s (s (s (s (s (s (s (s (s z))))))))))))) yes))

; A row with variables under the same head joins every ground answer.
!(add-atom &self (fact $y 9))
!(collapse (match &self (fact q 9) yes))
!(exists (fact q 9))
!(collapse (match &self (fact a 1) yes))
!(remove-atom &self (fact $y 9))
!(collapse (match &self (fact q 9) yes))
!(exists (fact q 9))

; Removing one occurrence keeps the other.
!(remove-atom &self (fact a 1))
!(collapse (match &self (fact a 1) yes))
!(add-atom &self (fact a 1))
!(collapse (match &self (fact a 1) yes))

; Many rows, removed and re-added in turn.
(= (fill $n) (if (== $n 0) done (let $u (add-atom &self (item $n (pair $n $n))) (fill (- $n 1)))))
(= (drain $n) (if (== $n 0) done (let $u (remove-atom &self (item $n (pair $n $n))) (drain (- $n 2)))))
!(fill 300)
!(drain 300)
!(collapse (match &self (item 300 (pair 300 300)) yes))
!(collapse (match &self (item 299 (pair 299 299)) yes))
!(exists (item 2 (pair 2 2)))
!(exists (item 1 (pair 1 1)))
!(length (collapse (match &self (item $n $p) $n)))

; A callable value admitted into a space is opaque to a public pattern.
(= (named $x $y) ($x $y))
!(collapse (let $f (named 1) (progn (add-atom &self (box $f)) (match &self (box (partial named (1))) leaked))))
