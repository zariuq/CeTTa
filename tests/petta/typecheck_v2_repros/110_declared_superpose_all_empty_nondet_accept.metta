; An all-empty superposition still carries the declared nondeterministic grade.

(: never-number (-[nondet]-> Number))
(= (never-number)
   (superpose ((empty) (empty))))

(: accept-number (-> Number Bool))
(= (accept-number $value) True)

(: use-never (-[nondet]-> Bool))
(= (use-never)
   (accept-number (never-number)))

!(test (collapse (use-never)) ())
