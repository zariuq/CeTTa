; The paths of two edges over a graph of (argv 1) nodes, each node with
; twelve edges, derived into a set.  Their relation is dense: kept as bits,
; it grows by a bit a pair; kept as rows, by a record and its index entries
; a fact.
!(bind! &paths (new-space set))
(= (out $k $i $n)
   (if (>= $i 12) done
       (let* (($t (% (+ (+ (* $k (+ (* 2 $i) 3)) (* $i $i)) 1) $n))
              ($a (add-atom &self (edge $k $t)))
              ($i1 (+ $i 1)))
         (out $k $i1 $n))))
(= (graph $k $n)
   (if (>= $k $n) done
       (let* (($o (out $k 0 $n))
              ($k1 (+ $k 1)))
         (graph $k1 $n))))
!(graph 0 (argv 1))
!(let $r (collapse (match &self (, (edge $x $y) (edge $y $z))
                          (add-atom &paths (path2 $x $z))))
      done)
!(size-atom (collapse (match &paths (path2 $x $z) t)))
