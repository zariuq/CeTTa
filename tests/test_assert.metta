; HE conformance: assert
; Tests truth assertion

; assert True passes
!(assert (== 1 1))

; assert equality
!(assert (== (+ 2 3) 5))
