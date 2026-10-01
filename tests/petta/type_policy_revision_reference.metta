(: pcg-revised (-> Number Atom))
(= (pcg-revised $x) $x)
!(pcg-revised (+ 2 4))
!(remove-atom &self (: pcg-revised (-> Number Atom)))
!(add-atom &self (: pcg-revised (-> Atom Atom)))
!(pcg-revised (+ 2 4))

; Holding is a clause-translation property across the declaration family,
; not a synonym for skipping the selected arrow's result guard.
(: pcg-body (-> Number Atom))
(: pcg-body (-> Number %Undefined%))
(= (pcg-body $x) (+ $x 1))
!(collapse (pcg-body 6))

(: pcg-partial (-> Number Number Number))
(= (pcg-partial $x $y) (+ $x $y))
!(let $f (pcg-partial 2) ($f 4))
!(collapse (let $f (pcg-partial 2) ($f true)))
