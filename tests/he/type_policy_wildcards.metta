; HE interpreter type matching uses Atom symmetrically. Structural type
; inference is a separate judgment, not a substitute for applicability.
(: pcg-unknown Atom)
(: pcg-number (-> Number Atom))
(= (pcg-number $x) (seen $x))
!(pcg-number pcg-unknown)
!(pcg-number 6)

; The cast after argument interpretation observes the same relation.
(: pcg-number-value (-> Number Number))
(= (pcg-number-value $x) $x)
!(pcg-number-value pcg-unknown)

; Expression as a codomain does not hold the equation body like Atom.
(: pcg-expression-result (-> Number Expression))
(= (pcg-expression-result $x) (+ $x 1))
!(pcg-expression-result 6)

; A concrete mismatch is not a wildcard.
(: pcg-wrong Other)
!(pcg-number pcg-wrong)
