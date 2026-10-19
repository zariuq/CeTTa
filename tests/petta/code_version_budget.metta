; Compiled code a running loop no longer needs is released: each step adds a
; rule, calls it and removes it, so each step compiles a new version of the
; rule's relation; and a transaction per step changes the authorities every
; call observes, so each step re-enters the same program.
(= (churn $n) (if (== $n 0) done (let $a (add-atom &self (= (rule $n) $n)) (let $v (rule $n) (let $r (remove-atom &self (= (rule $n) $n)) (churn (- $n 1)))))))
!(churn 2000)
(= (txloop $n) (if (== $n 0) done (let $x (transaction (+ $n 1)) (txloop (- $n 1)))))
!(txloop 20000)
