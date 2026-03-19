; Arithmetic operations
!(assertEqual (+ 1 2) 3)
!(assertEqual (- 10 3) 7)
!(assertEqual (* 4 5) 20)
!(assertEqual (/ 15 3) 5)
!(assertEqual (% 17 5) 2)

; Nested arithmetic
!(assertEqual (+ (* 3 4) (- 10 5)) 17)
!(assertEqual (* (+ 1 2) (+ 3 4)) 21)

; Comparison
!(assertEqual (< 1 2) True)
!(assertEqual (< 2 1) False)
!(assertEqual (> 5 3) True)
!(assertEqual (> 3 5) False)
!(assertEqual (== 42 42) True)
!(assertEqual (== 42 43) False)
