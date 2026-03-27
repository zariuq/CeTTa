; Float grounded type and mixed arithmetic
; Ref: metta.md grounded atoms

; Float arithmetic
!(assertEqual (+ 1.5 2.5) 4.0)
!(assertEqual (* 2.0 3.5) 7.0)
!(assertEqual (- 10.5 3.2) 7.3)
!(assertEqual (/ 7.0 2.0) 3.5)

; Mixed int/float promotion
!(assertEqual (+ 1 2.5) 3.5)
!(assertEqual (* 3 1.5) 4.5)

; Float comparison
!(assertEqual (< 1.5 2.5) True)
!(assertEqual (> 3.2 2.1) True)
