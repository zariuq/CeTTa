; The prepared register loop is deliberately narrower than ordinary PeTTa
; clause execution.  These cases pin its admitted aliasing shapes and the
; boundaries that must retain the canonical search machinery.

; The same ground value may occupy multiple call positions.  Tail arguments
; are assigned simultaneously, so neither update may observe the other.
(= (register-linear $n $a $b)
   (if (== $n 0)
       (+ $a $b)
       (register-linear (- $n 1) (+ $a 1) (+ $b 2))))
!(register-linear 4 5 5)

; One source register may feed multiple tail positions.  Immutable numeric
; values remain shared safely and are not consumed twice.
(= (register-repeat $n $a $discarded)
   (if (== $n 0)
       (+ $a $discarded)
       (register-repeat (- $n 1) (+ $a 1) (+ $a 1))))
!(register-repeat 2 5 99)

; A second equation excludes the head from singleton admission.  Both answers
; and their declaration order remain visible through the ordinary clause path.
(= (register-ambiguous $n $a)
   (if (== $n 0)
       $a
       (register-ambiguous (- $n 1) (+ $a 1))))
(= (register-ambiguous 0 $a) (+ $a 100))
!(collapse (register-ambiguous 2 0))

; Tabled and count-only collection calls retain their own scheduling and
; multiplicity machinery even when the underlying equation has register form.
!(tabled (register-tabled $n $a))
(= (register-tabled $n $a)
   (if (== $n 0)
       $a
       (register-tabled (- $n 1) (+ $a 1))))
!(register-tabled 4 10)
!(length (collapse (register-linear 3 1 2)))
