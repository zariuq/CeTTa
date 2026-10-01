; Native partial calls and authored partial calls keep distinct contracts.
!(collapse (+ 2))
!((+ 2) 4)
(: pcg-two (-> Number Number Atom))
(= (pcg-two $x $y) (+ $x $y))
!(collapse (pcg-two (+ 1 1)))
!((pcg-two (+ 1 1)) (+ 2 2))
; A Number codomain rejects an authored partial before it can be applied.
(: pcg-two-num (-> Number Number Number))
(= (pcg-two-num $x $y) (+ $x $y))
!(collapse (pcg-two-num 2))
