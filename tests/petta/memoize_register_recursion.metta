; A memoized relation keeps its table: the register accelerator that serves
; plain recursion stays off at the generator's root, so each argument is
; computed once (26 misses) and found again 23 times, as SWI-PeTTa does, and
; the doubly recursive definition runs in linear time.
!(import! &self (library lib_memo))
(= (fibm $n) (if (< $n 2) $n (+ (fibm (- $n 1)) (fibm (- $n 2)))))
!(memoize fibm)
!(fibm 25)
!(get-memoize-stats)
