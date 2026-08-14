; Candidate census probe: a nondeterministic contract permits dynamic evaluation.
(: evaluated-number-nondet (-[nondet]-> Number))
(= (evaluated-number-nondet) (eval (quote 1)))
