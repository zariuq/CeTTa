; The mode signature is checked once when this declaration block is admitted.
; Recursive execution then uses PeTTa's statically checked relation path: no
; runtime type obligation or gradual call-boundary judgment is required.

(: nik-static-countdown (-[det]-> Number Number))

(= (nik-static-countdown $n)
   (if (== $n 0)
       0
       (nik-static-countdown (- $n 1))))

!(nik-static-countdown 1000)
