; Two read ports over one relation of kept facts, with different patterns.
; A cursor names a kept fact among the facts its own pattern selects, so the
; tcalc engine, which tells rows apart by those names, declines these
; firings and the direct engine decides them.  Run with CETTA_SPACE_DENSE=eager
; so that the facts are kept in a relation from the first.
!(import! &self lib_transform)
!(bind! &s (new-space))
(= (fill $n)
   (if (< $n 64)
       (let $v (+ $n 1000)
         (let $_ (add-atom &s (e $n $v)) (fill (+ $n 1))))
       filled))
!(fill 0)
!(collapse (transform &s (, (e 5 $y) (e $x 1007)) (pair $y $x)))
!(collapse (transform &s (, (e $x 1007) (e 5 $y)) (pair2 $y $x)))
!(collapse (transform &s (, (e 5 $y) (take (e $x 1007))) (took $y $x)))
!(collapse (match &s (e 7 $y) $y))
!(collapse (transform &s (, (e 9 $y) (take (e $x 1010))) (took2 $y $x)))
!(collapse (match &s (e 10 $y) $y))
