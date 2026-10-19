; PeTTa refuses an authored equation for decons-atom, a static procedure, as
; SWI-PeTTa does: a program cannot change the decons-atom the entry guard
; reads.
(= (decons-atom ()) (nil ()))
(= (cnt ()) 0)
(= (cnt $xs) (let ($h $t) (decons-atom $xs) (+ 1 (cnt $t))))
!(println! (collapse (cnt ())))
!(println! (collapse (cnt (a))))
