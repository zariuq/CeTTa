; An error held while a success may still come is not published when the run
; is cut off before it ends.
(= (spin $n) (spin (+ $n 1)))
(= (g) (superpose ((/ 1 0) (spin 0))))
!(once (g))
