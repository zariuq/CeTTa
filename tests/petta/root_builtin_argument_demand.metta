; A root call of a list builtin observes its computed arguments, exactly as
; the same call does nested in an equation body.
!(cons-atom x (collapse (superpose (1 2))))
!(union-atom (collapse (superpose (1 2))) (a))
!(decons-atom (collapse (superpose (1 2))))
(= (nested) (cons-atom x (collapse (superpose (1 2)))))
!(nested)
!(size-atom (collapse (superpose (1 2))))
