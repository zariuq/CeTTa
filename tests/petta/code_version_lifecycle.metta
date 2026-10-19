; Compiled code lives as long as something runs it.  A call suspended
; under one definition keeps that definition for its remaining answers after
; the definition changes, while a new call enters the new one.
(= (p 1) a)
(= (p 2) b)
(= (p 3) c)
!(collapse (let $v (p $x) (if (== $x 1) (let $_ (remove-atom &self (= (p 3) c)) (let $_ (add-atom &self (= (p 4) d)) (pair $x $v (collapse (p $y))))) (pair $x $v ()))))
; A rule added, called and removed per step, and a transaction per step.
(= (churn $n) (if (== $n 0) done (let $a (add-atom &self (= (rule $n) $n)) (let $v (rule $n) (let $r (remove-atom &self (= (rule $n) $n)) (churn (- $n 1)))))))
!(churn 300)
(= (txloop $n) (if (== $n 0) done (let $x (transaction (+ $n 1)) (txloop (- $n 1)))))
!(txloop 300)
