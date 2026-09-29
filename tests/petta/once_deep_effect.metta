
; A depth-pruned branch may contain a future effect; the effect need not
; have been reached during the initial quantum.
(= (deep-write $n)
   (if (> $n 0) (deep-write (- $n 1))
       (progn (println! observed) (empty))))
(= (write-candidate) (deep-write 20000))
(= (write-candidate) ok)
!(once (write-candidate))
