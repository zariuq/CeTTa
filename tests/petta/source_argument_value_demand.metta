; PeTTa's function convention evaluates authored computation arguments before
; applying a value observer.  A variable occurrence is already a value: its
; substituted expression is observed as data rather than called again.

(= (demand-three) (a b c))
(= (demand-one) (z))
(= (demand-empty) ())
(= (demand-choice) (p))
(= (demand-choice) (q r))

!(println! (producer (size-atom (demand-three))))
!(println! (let-value
    (let $items (demand-three)
      (size-atom $items))))
!(println! (collapse-value
    (size-atom (collapse (demand-choice)))))
!(println! (nested-observer
    (size-atom (cdr-atom (demand-three)))))
!(println! (empty-value (size-atom (demand-empty))))
!(println! (one-value (size-atom (demand-one))))
!(println! (non-list-value (size-atom (+ 1 2))))
!(println! (literal-data (size-atom (and implies iff))))

(= (demand-observe-value $items)
   (size-atom $items))
!(println! (bound-value
    (demand-observe-value (quote (and implies iff)))))
