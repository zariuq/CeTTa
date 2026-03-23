; Test equation-based if (stdlib-defined via chain+unify)

; Basic truth values
!(assertEqual (if True yes no) yes)
!(assertEqual (if False yes no) no)

; Condition needs evaluation
!(assertEqual (if (== 1 1) yes no) yes)
!(assertEqual (if (== 1 2) yes no) no)
!(assertEqual (if (< 3 5) less greater) less)
!(assertEqual (if (> 3 5) less greater) greater)

; Nested if
!(assertEqual (if True (if False inner-a inner-b) outer) inner-b)

; If with arithmetic in branches
!(assertEqual (if True (+ 1 2) (+ 3 4)) 3)
!(assertEqual (if False (+ 1 2) (+ 3 4)) 7)

; Recursive function using if (tests TCO)
(= (countdown $n)
   (if (<= $n 0) done (countdown (- $n 1))))
!(assertEqual (countdown 10) done)
!(assertEqual (countdown 100) done)

; Accumulator pattern
(= (sum-to $n $acc)
   (if (<= $n 0) $acc (sum-to (- $n 1) (+ $acc $n))))
!(assertEqual (sum-to 10 0) 55)

; Meta-reasoning: stdlib if equation is introspectable
!(assertEqual
  (> (size-atom (collapse (match &self (= (if $c $t $e) $r) $c))) 0)
  True)
