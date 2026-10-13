; and-then and or-else decide by their first operand and run the second
; only when the first does not decide; bindings of the first reach the
; second.
!(test (and-then (== 1 1) (== 2 2)) true)
!(test (and-then (== 1 2) (== 2 2)) false)
!(test (or-else (== 1 1) (== 2 3)) true)
!(test (or-else (== 1 2) (== 3 3)) true)
!(test (and-then (= $pair (two 1 2)) (== $pair (two 1 2))) true)
!(test (collapse (and-then (superpose (true false)) (== 1 1))) (true false))
