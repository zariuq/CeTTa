; Candidate census probe: dynamic evaluation preserves an explicit assertion.
(: evaluated-number-nondet (-[nondet]-> Number))
(= (evaluated-number-nondet) (eval (quote (the Number 1))))
