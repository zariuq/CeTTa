; An undeclared producer has both Number and String results.  Typecheck-v2
; may join those alternatives while checking the body, but it does not
; publish the compound union as an inferred contract.  Default mode therefore
; keeps the producer usable without claiming more knowledge than it has.

(= (mixed-result)
   (superpose (1 "text")))

(: accept-mixed (-> (| Number String) Bool))
(= (accept-mixed $value) True)

(: use-mixed (-> Bool))
(= (use-mixed)
   (accept-mixed (mixed-result)))
