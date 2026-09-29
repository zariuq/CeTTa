; A choice's binding compaction keeps each live frame with its schema and the
; slots it grew past it, before it keeps any binding whose value names such a
; slot.  Equations that overlap on their heads leave host goals whose bindings
; name grown slots; every answer is SWI-PeTTa's.
(= (pick-k $l 0) (let $t True (())))
(= (pick-k () $k) (let $t True ()))
(= (pick-k (cons $h $t) $k)
   (append (map-atom (pick-k $t (- $k 1)) (|-> ($c) (cons $h $c)))
           (pick-k $t $k)))
!(collapse (pick-k (0 1 2) 2))
!(size-atom (collapse (pick-k (0 1 2 3) 2)))
!(size-atom (collapse (pick-k (0 1 2 3 4) 2)))
