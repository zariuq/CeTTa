; A Python object lives while some atom of it is live: one that a form only
; computes with is released after the form, and one that a space or a state
; keeps survives, intact, into later forms.  `alive` counts the objects
; Python still has; SWI-PeTTa releases none of them, so those lines are
; CeTTa's own.

!(py-call (foreign_holds_probe.alive))
!(let $o (py-call (foreign_holds_probe.make 1)) (py-call (foreign_holds_probe.ident $o)))
!(py-call (foreign_holds_probe.alive))

; A space keeps one.
!(let $o (py-call (foreign_holds_probe.make 2)) (add-atom &self (kept $o)))
!(match &self (kept $o) (py-call (foreign_holds_probe.ident $o)))
!(py-call (foreign_holds_probe.alive))

; A state keeps one.
!(bind! &cell (new-state (py-call (foreign_holds_probe.make 4))))
!(py-call (foreign_holds_probe.ident (get-state &cell)))
!(py-call (foreign_holds_probe.alive))

; A long loop keeps one across its collections.
(= (spin $o $n)
   (if (< $n 1)
       (py-call (foreign_holds_probe.ident $o))
       (spin $o (- $n 1))))
(= (spin-kept $n) (match &self (kept $o) (spin $o $n)))
!(spin-kept 200000)
!(let $o (py-call (foreign_holds_probe.make 5)) (spin $o 200000))
!(py-call (foreign_holds_probe.alive))

; Many objects computed with in one form are all released after it.
(= (churn $n $sum)
   (if (< $n 1)
       $sum
       (churn (- $n 1)
              (+ $sum (py-call (foreign_holds_probe.ident
                                (py-call (foreign_holds_probe.make $n))))))))
!(churn 1000 0)
!(py-call (foreign_holds_probe.alive))
