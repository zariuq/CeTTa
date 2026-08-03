(= (gc-map $f ()) ())
(= (gc-map $f (cons $x $xs))
   (cons ($f $x) (gc-map $f $xs)))

(= (gc-range $n)
   (if (== $n 0)
       ()
       (cons $n (gc-range (- $n 1)))))

; These three exact answers also expose machine-work and heap curves to the
; Makefile gate.  Each rung doubles the logical input.
!(test
   (let $items (gc-map (+ 1) (gc-range 8000))
        (length $items))
   8000)

!(test
   (let $items (gc-map (+ 1) (gc-range 16000))
        (length $items))
   16000)

!(test
   (let $items (gc-map (+ 1) (gc-range 32000))
        (length $items))
   32000)
