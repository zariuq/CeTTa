; A program that declares Predicate's parameter Atom keeps its argument as
; written, as for any function.
(: Predicate (-> Atom %Undefined%))
!(let $p (Predicate (+ $in 1)) ok)
!(Predicate (f (+ 1 2)))
!(collapse (callPredicate (Predicate (= $y done))))
!(let $_ (assertzPredicate (Predicate (fact (+ 1 2))))
    (collapse (let $_ (callPredicate (Predicate (fact $v))) $v)))
