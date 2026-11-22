; A get-type rule added while a body runs gives the literal 0 a second
; answer at Number.  A literal's guard commits to its first answer, its
; class, so the calls after the rule is added still run once.
(: + (-> Number Number Number))
(= (g $x) (+ (let $_ (add-atom &self (= (get-type 0) Number)) $x) 0))
!(collapse (g 5))
!(collapse (g 6))
