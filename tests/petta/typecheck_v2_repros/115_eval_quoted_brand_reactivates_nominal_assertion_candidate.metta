; Candidate census probe: evaluation reactivates a held nominal assertion.
(: Count (Newtype Number))

(: evaluated-count (-[det]-> Count))
(= (evaluated-count) (eval (quote (brand Count 1))))
