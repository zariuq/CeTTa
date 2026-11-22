; An all-empty superposition retains a nondeterministic grade and is rejected
; by a semideterministic declaration.

(: never-number (-[semidet]-> Number))
(= (never-number)
   (superpose ((empty) (empty))))

(: accept-number (-> Number Bool))
(= (accept-number $value) True)

(: use-never (-[semidet]-> Bool))
(= (use-never)
   (accept-number (never-number)))

!(test (collapse (use-never)) ())
