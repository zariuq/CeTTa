(= (color red) warm)
(= (color blue) cool)

; Each occurrence independently matches the same authored equations.  The
; outer bag retains both routes and multiplicity without observing scheduling
; or the physical representation of branch-local variables.
(= (open-family)
   (msort
     (collapse
       (hyperpose
         ((match &self (= (color $x) $tone) ($x $tone))
          (match &self (= (color $x) $tone) ($tone $x)))))))

; A branch family may itself arrive through an ordinary logical variable.
(= (computed-family $branches)
   (msort (collapse (hyperpose $branches))))

!(test (open-family)
       ((blue cool) (cool blue) (red warm) (warm red)))
!(test (computed-family ((+ 40 2) (+ 40 3) (+ 40 2)))
       (42 42 43))
