; Two deep non-tail traversals, one after the other.  Each holds a frame
; identity per pending level; the first's are dead once its answer is cut,
; but its activations keep theirs until a collection reaches them.  Under a
; bound on identities the second traversal needs those back: a collection of
; every generation runs when the identities left run low, so the bound need
; only cover the identities live at once.  Both together allocate far less
; than the heap's major window, so no collection of its own reaches them.
(= (map-flat $f ()) ())
(= (map-flat $f (cons $x $xs)) (cons ($f $x) (map-flat $f $xs)))
(= (range $n) (if (== $n 0) () (cons $n (range (- $n 1)))))
!(let $a (once (length (map-flat (+ 1) (range 2000)))) (let $b (length (map-flat (+ 1) (range 2000))) ($a $b)))
