; Reference contract for the base PeTTa type policy.
; Expected output comes from the pinned SWI-PeTTa source, not CeTTa.

(: pcg-pair (-> Number Atom Expression))
(: pcg-pair (-> Number Atom Expression))
(= (pcg-pair $x $y) (pair $x $y))
!(pcg-pair (+ 2 4) (+ 4 2))

(: pcg-raw (-> Atom Atom))
(= (pcg-raw $x) $x)
!(pcg-raw (println! SHOULD-NOT-RUN))

(: pcg-expr (-> Expression Expression))
(= (pcg-expr $x) $x)
!(collapse (pcg-expr (+ 2 4)))
!(collapse (pcg-expr (tag a)))

(: pcg-free (-> %Undefined% %Undefined%))
(= (pcg-free $x) $x)
!(pcg-free (+ 2 4))

(: pcg-held (-> Number Atom))
(= (pcg-held $x) (witness (+ $x 1)))
!(pcg-held 6)

(: pcg-same (-> $t $t))
(= (pcg-same $x) $x)
!(pcg-same 7)
!(pcg-same true)

(: pcg-alpha (-> $a $a))
(: pcg-alpha (-> $b $b))
(= (pcg-alpha $x) $x)
!(pcg-alpha 9)

(: pcg-bad (-> $t $t))
(= (pcg-bad $x) true)
!(collapse (pcg-bad 7))

(: pcg-over (-> Number Atom))
(: pcg-over (-> Atom Atom))
(= (pcg-over $x) $x)
!(pcg-over (+ 2 4))

(: pcg-see-expr (-> Expression Atom))
(= (pcg-see-expr $x) $x)
!(collapse (pcg-see-expr (+ 2 4)))

(: pcg-runbody (-> Number %Undefined%))
(= (pcg-runbody $x) (+ $x 1))
!(pcg-runbody 6)
