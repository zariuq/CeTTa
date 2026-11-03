; The fan-out with its filter at top level: the query runs on the compiled
; tier as its equation form does (query_body_fanout_wrapped.metta).
(= (pick) (superpose (0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63)))
(= (walk $k $acc) (if (== $k 0) $acc (let $p (pick) (walk (- $k 1) (+ (* $acc 7) $p)))))
!(size-atom (collapse (let $s (walk 2 0) (if (== (% $s 97) 0) $s (empty)))))
