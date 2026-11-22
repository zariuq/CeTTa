; A conditional whose two branches produce no result remains semideterministic.

(= (never-number $flag)
   (if $flag (empty) (empty)))

(: accept-number (-> Number Bool))
(= (accept-number $value) True)

(: use-never (-[semidet]-> Bool Bool))
(= (use-never $flag)
   (accept-number (never-number $flag)))

!(test (collapse (use-never True)) ())
!(test (collapse (use-never False)) ())
