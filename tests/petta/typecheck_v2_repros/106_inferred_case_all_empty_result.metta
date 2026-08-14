; A case whose branches all produce no result has no inferred result type.

(= (never-number $flag)
   (case $flag ((True (empty)) (False (empty)))))

(: accept-number (-> Number Bool))
(= (accept-number $value) True)

(: use-never (-[semidet]-> Bool Bool))
(= (use-never $flag)
   (accept-number (never-number $flag)))

!(test (collapse (use-never True)) ())
!(test (collapse (use-never False)) ())
