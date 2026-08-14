; An all-empty superposition has no inferred result type in strict mode.

(= (never-number)
   (superpose ((empty) (empty))))

(: accept-number (-> Number Bool))
(= (accept-number $value) True)

(: use-never (-[semidet]-> Bool))
(= (use-never)
   (accept-number (never-number)))

!(test (collapse (use-never)) ())
