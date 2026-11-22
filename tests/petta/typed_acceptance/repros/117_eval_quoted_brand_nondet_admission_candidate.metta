; Candidate census probe: an explicitly nondeterministic contract admits eval.
(: Count (Newtype Number))

(: evaluated-number-nondet (-[nondet]-> Number))
(= (evaluated-number-nondet) (eval (quote (brand Count 1))))
