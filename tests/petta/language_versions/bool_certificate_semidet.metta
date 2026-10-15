; Every clause of flag-of yields true or false, so its call is a bound Bool
; and and/2 over it is only a test: the caller keeps flag-of's semidet.
(: flag-of (-[semidet]-> Bool Bool))
(= (flag-of true) (case true ((true true) (false false))))

(: flag-and-true (-[semidet]-> Bool Bool))
(= (flag-and-true $b) (and (flag-of $b) true))

!(test (flag-and-true true) true)
