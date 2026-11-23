; (math:clamp lo hi x) is (math:max lo (math:min x hi)): the input x whenever
; lo <= x <= hi by value, a tie with either bound included (x is kept, never
; an equal-valued bound), otherwise the bound x crosses.  A NaN operand
; propagates; where lo exceeds hi the answer is lo.
!(import! &self math)
; a tie with the lower bound, then the upper, each held another way
!(math:clamp 1 2 1.0)
!(math:clamp 1 2 2.0)
!(math:clamp 0.0 1.0 -0.0)
!(math:clamp -0.0 0.0 0.0)
!(math:clamp -1.0 0.0 -0.0)
!(math:clamp 1.0 2 1)
!(math:clamp 1 2.0 2)
!(math:clamp 0.5 2 1/2)
!(math:clamp 1 100000000000000000000 1.0e20)
; strictly inside, strictly outside
!(math:clamp 1 3 2)
!(math:clamp 0.5 1.5 1)
!(math:clamp 1 2 0)
!(math:clamp 1 2 3.5)
!(math:clamp 1.0 2.0 -5)
; NaN
!(math:clamp 0.0 1.0 (/ 0.0 0.0))
!(math:clamp (/ 0.0 0.0) 1.0 0.5)
; lo above hi
!(math:clamp 3 1 2)
!(math:clamp 3.0 1 5)
