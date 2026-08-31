(= (segment-permit keep) ok)
(= (segment-permit drop) (empty))

; One open occurrence separates a candidate-local scalar tree from the next
; recursive call.  The condition intentionally mixes arithmetic, comparison,
; and Boolean structure so the benchmark measures segment execution rather
; than one privileged operator or relation name.
(= (scalar-walk $n keep)
   (let $_ (segment-permit keep)
     (if (and (>= (* (+ $n 1) 1) 1) (> $n 0))
         (scalar-walk (- $n 1) keep)
         done)))
(= (scalar-walk $n drop)
   (let $_ (segment-permit drop)
     (if (and (>= (* (+ $n 1) 1) 1) (> $n 0))
         (scalar-walk (- $n 1) drop)
         never)))

!(scalar-walk 300000 $lane)
