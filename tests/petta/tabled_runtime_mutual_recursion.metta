;; Tabled mutual left recursion gives the same answers whether its equations
;; are written in the file or added while the program runs.
!(import! &self (library lib_tabling))
(= (ta (cell a $t)) $t)
(= (tb (cell b $t)) $t)

!(tabled (s1 $x))
!(tabled (i1 $x))
(= (s1 $x) (let $z (i1 $x) (tb $z)))
(= (i1 $x) (let $z (s1 $x) (tb $z)))
(= (i1 $x) (let $z (ta $x) (tb $z)))
!(collapse (s1 (cell a (cell b (cell b (cell b (cell b nil)))))))

!(add-atom &self (= (s2 $x) (let $z (i2 $x) (tb $z))))
!(add-atom &self (= (i2 $x) (let $z (s2 $x) (tb $z))))
!(add-atom &self (= (i2 $x) (let $z (ta $x) (tb $z))))
!(tabled (s2 $x))
!(tabled (i2 $x))
!(collapse (s2 (cell a (cell b (cell b (cell b (cell b nil)))))))
;; Negative: no a, so no derivation.
!(collapse (s2 (cell b (cell b (cell b nil)))))

;; Declared before its equations are added.
!(tabled (s3 $x))
!(tabled (i3 $x))
!(add-atom &self (= (s3 $x) (let $z (i3 $x) (tb $z))))
!(add-atom &self (= (i3 $x) (let $z (s3 $x) (tb $z))))
!(add-atom &self (= (i3 $x) (let $z (ta $x) (tb $z))))
!(collapse (s3 (cell a (cell b (cell b (cell b nil))))))
