(= (minmax-loop $remaining $value)
   (if (< $remaining 1) $value
     (minmax-loop (- $remaining 1)
                  (max (min $value 2.5) 0.5))))

!(min 2.5 1.5)
!(max 2 4.5)
; On a tie min answers its first operand and max its second.
!(min 1 1.0)
!(max 1.0 1)
!(minmax-loop 1000 1.5)
