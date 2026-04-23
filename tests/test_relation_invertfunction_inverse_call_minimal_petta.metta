; Smallest current inverse-call witness distilled from
; hyperon/PeTTa/examples/invertfunction.metta:
; inverse destructuring through append works, but inverse matching against
; a returned head containing #+ still misses before any body export matters.

(= (g $X $Y $Z)
   (append ((#+ $X $Z)) $Y))

!(test (let (g $X $Y 35) (42 2 3) ok)
       ok)
