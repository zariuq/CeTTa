; Two witnesses: `d`, which depth-first order reaches after exhausting a
; failing subtree of 2^19 paths, and a shallow `s`.  The default
; first-witness strategy may return either; an explicit depth-first
; controller returns `d`.
(= (hf z) (empty))
(= (hf (s $n)) (hf $n))
(= (hf (s $n)) (hf $n))
(= (hard z) d)
(= (hard (s $n)) (hf $n))
(= (hard (s $n)) (hard $n))
(= (w) (hard (s (s (s (s (s (s (s (s (s (s (s (s (s (s (s (s (s (s (s (s z))))))))))))))))))))))
(= (w) s)
!(once (w))
