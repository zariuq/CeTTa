; A match inside an equation reads a relation while the effects of its
; solutions remove and add the relation's facts: the read keeps the rows
; and facts it began with, as a match reads its first pattern once.
; Distances relaxed with updates that take effect at once: the space's
; first facts of sd are rows and its later ones a relation, and relaxing
; from a row removes a fact of the relation while rows remain to be read.
!(bind! &s (new-space set))
!(bind! &w (new-space set))
!(add-atom &s (sd 0 0))
(= (fill $k $n) (if (>= $k $n) done (let $_ (add-atom &s (sd $k 100000)) (fill (+ $k 1) $n))))
!(fill 1 4200)
!(add-atom &w (e 0 4150 1))
!(add-atom &w (e 0 4151 2))
!(add-atom &w (e 4150 4152 1))
!(add-atom &w (e 4151 4152 5))
(= (improve $v $nd)
   (let $cur (collapse (match &s (sd $v $y) $y))
     (if (== $cur ())
         (let $_ (add-atom &s (sd $v $nd)) $v)
         (let ($y) $cur
           (if (< $nd $y)
               (let* (($_ (remove-atom &s (sd $v $y))) ($_ (add-atom &s (sd $v $nd)))) $v)
               (empty))))))
(= (relax $u) (match &s (sd $u $d) (match &w (e $u $v $c) (improve $v (+ $d $c)))))
(= (sssp $front $k)
   (let $next (collapse (unique (let $u (superpose $front) (relax $u))))
     (if (== $next ()) $k (sssp $next (+ $k 1)))))
!(sssp (0) 0)
!(sort-atom (collapse (match &s (sd $v $d) (if (< $d 100000) ($v $d) (empty)))))
!(size-atom (collapse (match &s (sd $v $d) t)))
