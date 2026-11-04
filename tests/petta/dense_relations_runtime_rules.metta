; Functions added while the space keeps facts in relations.  The facts in
; relations follow the rows, so a rule added later is a row placed before
; them: every reader of the space's rules reads it, those that read the
; rules before it included.
;
; One fact, kept in a relation from the first under eager relations; then
; two functions, the second read after the first.
(vrowh vf 0)
!(add-atom &self (= (lf 9) 1))
!(add-atom &self (= (lg 1) 5))
!(collapse (lg 1))
!(collapse (lf 9))
; A relation formed past the threshold of rows by the program's own adds;
; then a function read, and one added after it.
(= (fill $n)
   (if (> $n 0)
       (let $_ (add-atom &self (wrow $n)) (fill (- $n 1)))
       done))
!(add-atom &self (= (la 2) 3))
!(fill 4200)
!(add-atom &self (= (lb 1) 2))
!(lb 1)
!(add-atom &self (= (lc 2) 3))
!(lc 2)
!(la 2)
; A compiled function whose answers hold variables reads its callee's rules;
; an unrelated rule added after it leaves it as it was, and another rule of
; the callee is read.
(= (g 1) (a $u))
(= (f $x) (g $x))
!(size-atom (collapse (f 1)))
!(add-atom &self (= (h 0) $w))
!(size-atom (collapse (f 1)))
!(add-atom &self (= (g 1) (b $v)))
!(size-atom (collapse (f 1)))
!(size-atom (collapse (g 1)))
; The facts stay where they were.
!(size-atom (collapse (match &self (wrow $n) $n)))
!(size-atom (collapse (match &self (vrowh $a $b) $a)))
