(= (minmax-loop $remaining $value)
   (if (< $remaining 1) $value
     (minmax-loop (- $remaining 1)
                  (max (min $value 2.5) 0.5))))

!(min 2.5 1.5)
!(max 2 4.5)
; A tie returns the left operand, retaining its exact representation.
!(min 1 1.0)
!(max 1.0 1)
!(minmax-loop 1000 1.5)
