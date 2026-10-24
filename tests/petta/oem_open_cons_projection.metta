; An open-cons value has a head and tail even when its tail is not a list.
; The open-equation region may borrow either selected child without
; materializing or closing the cell.
(= (cell-head $head $tail)
   (let $cell (cons-atom $head $tail) (car-atom $cell)))
(= (cell-tail $head $tail)
   (let $cell (cons-atom $head $tail) (cdr-atom $cell)))
(= (head-after-tail-binding $head)
   (let $cell (cons-atom $head $tail)
     (let $tail (z) (car-atom $cell))))
(= (tail-after-binding $head)
   (let $cell (cons-atom $head $tail)
     (let $tail (z) (cdr-atom $cell))))

!(cell-head a atom)
!(cell-tail a atom)
!(head-after-tail-binding a)
!(tail-after-binding a)

; Two projections per iteration make the receipt count an exact route test.
(= (projection-loop $remaining $cell)
   (if (< $remaining 1) done
     (let* (($head (car-atom $cell))
            ($tail (cdr-atom $cell)))
       (projection-loop (- $remaining 1)
                        (cons-atom $head $tail)))))
!(projection-loop 1000 (cons-atom a atom))
