; Equation indirection must not hide the semantic operation from the scheduler
; authority.  Identical values avoid imposing an incidental scheduler order.

(= (named-occurrence-hyperpose)
   (hyperpose ((+ 2 1) (+ 2 1))))

!(named-occurrence-hyperpose)
