; A determinate pure producer may be consumed as a pull stream when the
; bound collection is observed only through length.  The producer algebra
; itself is ordinary PeTTa; no counted representation appears in source.
(= (pull-range $n)
   (if (== $n 0)
       ()
       (cons $n (pull-range (- $n 1)))))

(= (pull-map $f ()) ())
(= (pull-map $f (cons $x $xs))
   (cons ($f $x) (pull-map $f $xs)))

!(let $items (pull-map (+ 1) (pull-range 0))
      (length $items))
!(let $items (pull-map (+ 1) (pull-range 1))
      (length $items))
!(let $items (pull-map (+ 1) (pull-range 6))
      (length $items))
!(let $items (pull-map (+ 1) (pull-range 4))
      (pair (length $items) (length $items)))

; A materializing observation rejects the fold certificate.  The exact
; ordered list remains visible through canonical evaluation.
!(let $items (pull-map (+ 1) (pull-range 4))
      (pair (length $items) $items))

; Overlapping clauses make the mapper nondeterminate.  The prepared producer
; must decline, preserving canonical multiplicity and authored answer order.
(= (pull-choice $x) $x)
(= (pull-choice $x) (+ $x 10))
!(let $items (pull-map pull-choice (pull-range 3))
      (length $items))

; An effectful mapper is never admitted by the pure producer compiler.  Its
; effects remain in canonical element order and occur exactly once.
(= (pull-effect $x)
   (let $_ (println! $x) $x))
!(let $items (pull-map pull-effect (pull-range 2))
      (length $items))

; A pure mapper fault is not erased by a cardinality consumer.  The pull
; attempt declines at the fault and canonical evaluation owns the result.
(= (pull-fault $x) (/ 10 (- $x 2)))
!(let $items (pull-map pull-fault (pull-range 3))
      (length $items))
