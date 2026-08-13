; This intentionally places a gradual arrow boundary on every recursive call.
; It is the negative performance control for the statically admitted variant:
; both programs return the same value, but this one must exercise runtime type
; obligations rather than masquerading as a zero-cost interior.

(: nik-gradual-countdown (-> Number Number))

(= (nik-gradual-countdown $n)
   (if (== $n 0)
       0
       (nik-gradual-countdown (- $n 1))))

!(nik-gradual-countdown 1000)
