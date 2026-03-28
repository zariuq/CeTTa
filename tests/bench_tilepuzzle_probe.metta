; ============================================================================
; 8-Puzzle bounded BFS probe for CeTTa runtime profiling
; ============================================================================

(= (move (___ $b $c $d $e $f $g $h $i) R) ($b ___ $c $d $e $f $g $h $i))
(= (move (___ $b $c $d $e $f $g $h $i) D) ($d $b $c ___ $e $f $g $h $i))

(= (move ($a ___ $c $d $e $f $g $h $i) L) (___ $a $c $d $e $f $g $h $i))
(= (move ($a ___ $c $d $e $f $g $h $i) R) ($a $c ___ $d $e $f $g $h $i))
(= (move ($a ___ $c $d $e $f $g $h $i) D) ($a $e $c $d ___ $f $g $h $i))

(= (move ($a $b ___ $d $e $f $g $h $i) L) ($a ___ $b $d $e $f $g $h $i))
(= (move ($a $b ___ $d $e $f $g $h $i) D) ($a $b $f $d $e ___ $g $h $i))

(= (move ($a $b $c ___ $e $f $g $h $i) U) (___ $b $c $a $e $f $g $h $i))
(= (move ($a $b $c ___ $e $f $g $h $i) R) ($a $b $c $e ___ $f $g $h $i))
(= (move ($a $b $c ___ $e $f $g $h $i) D) ($a $b $c $g $e $f ___ $h $i))

(= (move ($a $b $c $d ___ $f $g $h $i) U) ($a ___ $c $d $b $f $g $h $i))
(= (move ($a $b $c $d ___ $f $g $h $i) L) ($a $b $c ___ $d $f $g $h $i))
(= (move ($a $b $c $d ___ $f $g $h $i) R) ($a $b $c $d $f ___ $g $h $i))
(= (move ($a $b $c $d ___ $f $g $h $i) D) ($a $b $c $d $h $f $g ___ $i))

(= (move ($a $b $c $d $e ___ $g $h $i) U) ($a $b ___ $d $e $c $g $h $i))
(= (move ($a $b $c $d $e ___ $g $h $i) L) ($a $b $c $d ___ $e $g $h $i))
(= (move ($a $b $c $d $e ___ $g $h $i) D) ($a $b $c $d $e $i $g $h ___))

(= (move ($a $b $c $d $e $f ___ $h $i) U) ($a $b $c ___ $e $f $d $h $i))
(= (move ($a $b $c $d $e $f ___ $h $i) R) ($a $b $c $d $e $f $h ___ $i))

(= (move ($a $b $c $d $e $f $g ___ $i) U) ($a $b $c $d ___ $f $g $e $i))
(= (move ($a $b $c $d $e $f $g ___ $i) L) ($a $b $c $d $e $f ___ $g $i))
(= (move ($a $b $c $d $e $f $g ___ $i) R) ($a $b $c $d $e $f $g $i ___))

(= (move ($a $b $c $d $e $f $g $h ___) U) ($a $b $c $d $e ___ $g $h $f))
(= (move ($a $b $c $d $e $f $g $h ___) L) ($a $b $c $d $e $f $g ___ $h))

!(bind! &seen (new-space hash))

(= (try-add-unique $state)
   (case (collapse (match &seen (seen $state) found))
     ((() (let () (add-atom &seen (seen $state)) $state)))))

(= (enqueue-all $q ()) ())
(= (enqueue-all $q $items)
   (let ($head $tail) (decons-atom $items)
     (let () (space-push $q $head)
       (enqueue-all $q $tail))))

(= (bfs-step-limited $q $n $limit)
   (if (== $n $limit)
       $n
       (if (== (space-len $q) 0)
           $n
           (let* (($state (space-pop $q))
                  ($neighbors (collapse
                    (let $snew (move $state $dir)
                      (try-add-unique $snew))))
                  (() (enqueue-all $q $neighbors)))
             (bfs-step-limited $q (+ $n 1) $limit)))))

(= (bfs-all-limited $start $limit)
   (let* ((() (add-atom &seen (seen $start)))
          ($q (new-space queue))
          (() (space-push $q $start)))
     (bfs-step-limited $q 0 $limit)))

!(reset-runtime-stats!)
!(bfs-all-limited (___ 1 2 3 4 5 6 7 8) 5000)
!(space-len &seen)
!(match (runtime-stats!) (runtime-counter query-equations $n) $n)
!(match (runtime-stats!) (runtime-counter query-equation-candidates $n) $n)
!(match (runtime-stats!) (runtime-counter query-equation-legacy-candidates $n) $n)
!(match (runtime-stats!) (runtime-counter query-equation-subst-candidates $n) $n)
!(match (runtime-stats!) (runtime-counter rename-vars $n) $n)
!(match (runtime-stats!) (runtime-counter hash-space-exact-lookup $n) $n)
!(match (runtime-stats!) (runtime-counter hash-space-exact-hit $n) $n)
!(match (runtime-stats!) (runtime-counter space-push $n) $n)
!(match (runtime-stats!) (runtime-counter space-pop $n) $n)
!(match (runtime-stats!) (runtime-counter queue-compact $n) $n)
!(match (runtime-stats!) (runtime-counter resolve-registry-hit $n) $n)
!(match (runtime-stats!) (runtime-counter resolve-registry-rewrite $n) $n)
!(match (runtime-stats!) (runtime-counter resolve-registry-noop $n) $n)
!(match (runtime-stats!) (runtime-counter bindings-clone $n) $n)
!(match (runtime-stats!) (runtime-counter bindings-apply $n) $n)
!(match (runtime-stats!) (runtime-counter bindings-free $n) $n)
!(match (runtime-stats!) (runtime-counter query-equation-subst-emitted $n) $n)
!(match (runtime-stats!) (runtime-counter query-equation-subst-candidate-fallback $n) $n)
!(match (runtime-stats!) (runtime-counter query-equation-subst-bucket-fallback $n) $n)
