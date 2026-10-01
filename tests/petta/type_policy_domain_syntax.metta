; Base PeTTa does not interpret a compound type as a dependent binder.
(: pcg-bound (-> (: $x Number) Atom))
(= (pcg-bound $x) (seen $x))
!(collapse (pcg-bound (+ 2 4)))
(: pcg-held (-> (: $x Atom) Atom))
(= (pcg-held $x) (seen $x))
!(collapse (pcg-held (+ 2 4)))
; Literal Number and Atom domains remain the ordinary positive controls.
(: pcg-number (-> Number Atom))
(= (pcg-number $x) (seen $x))
!(pcg-number (+ 2 4))
(: pcg-atom (-> Atom Atom))
(= (pcg-atom $x) (seen $x))
!(pcg-atom (+ 2 4))
