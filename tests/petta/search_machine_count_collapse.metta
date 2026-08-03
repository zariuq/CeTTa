; COUNT over COLLAPSE is a fold over the exact answer bag.  Duplicate
; occurrences count independently, the PeTTa unit tuple remains an ordinary
; result, and a genuinely failed branch contributes no result.
(counted a)
(counted a)
(counted b)
(counted-dynamic-left count-dynamic-marker)
(counted-dynamic-left count-dynamic-marker)
(counted-dynamic-right count-dynamic-marker)
(counted-dynamic-right other-marker)

(= (counted-items)
   (collapse (match &self (counted $x) $x)))

!(length (collapse (match &self (counted $x) $x)))
!(length (collapse (match &self (counted $x) ())))
!(length (collapse (superpose (a a ()))))
!(length (collapse (empty)))

; A collection returned through an ordinary relation may remain a count while
; every observation is length.  Repeated observations reuse that exact count.
!(let* (($items (counted-items)))
       (pair (length $items) (length $items)))

; Any materializing observation rejects count-only fusion.  The original bag,
; including duplicate occurrences and order, remains visible.
!(let* (($items (counted-items)))
       (pair (length $items) $items))

; A variable relation head is data in a match template, not a call to
; evaluate.  COUNT may therefore stay below reconstruction while preserving
; duplicates and all matching heads.
!(length
   (collapse
     (match &self
            ($relation count-dynamic-marker)
            ($relation count-dynamic-marker))))

; The same shape outside COUNT remains fully materialized.
!(collapse
   (match &self
          ($relation count-dynamic-marker)
          ($relation count-dynamic-marker)))
