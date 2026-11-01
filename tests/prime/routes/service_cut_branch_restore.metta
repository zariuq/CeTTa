; A late alternative restores a long retained service prefix. Equal code
; across cuts retains each reply's value, origin and source order.
(= (cut:next K 2) 1)
(= (cut:next K 2) -1)
(= (cut:next K $n) (if (== $n 2) (empty) (- $n 1)))
(= (cut:walk $n)
  (if (<= $n 0) (finished $n) (cut:walk (cut:next K $n))))

!(collapse (cut:walk 70))
!(collapse (cut:walk 131))
!(collapse (superpose ((cut:walk 70) (cut:walk 70))))
