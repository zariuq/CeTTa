; Reference contract for the HE type policy. Unlike PeTTa, an Expression
; formal can hold a source expression before it is evaluated.

(: pcg-raw (-> Atom Atom))
(= (pcg-raw $x) $x)
!(pcg-raw (println! SHOULD-NOT-RUN))

(: pcg-expr (-> Expression Atom))
(= (pcg-expr $x) $x)
!(pcg-expr (+ 2 4))
!(pcg-expr (tag a))

(: pcg-free (-> %Undefined% %Undefined%))
(= (pcg-free $x) $x)
!(pcg-free (+ 2 4))

(: pcg-held (-> Number Atom))
(= (pcg-held $x) (witness (+ $x 1)))
!(pcg-held 6)

(: pcg-same (-> $t $t))
(= (pcg-same $x) $x)
!(pcg-same 7)

(: pcg-over (-> Number Atom))
(: pcg-over (-> Atom Atom))
(= (pcg-over $x) $x)
!(pcg-over (+ 2 4))

(: pcg-runbody (-> Number %Undefined%))
(= (pcg-runbody $x) (+ $x 1))
!(pcg-runbody 6)

; The reference runner stops the document on this error, so keep it last.
(: pcg-bad (-> $t $t))
(= (pcg-bad $x) "wrong")
!(pcg-bad 7)
