; Tilepuzzle capped at 5000 BFS steps for profiling with valgrind/massif/DHAT.
; Same logic as test_tilepuzzle.metta but stops early.

; ---- Move rules (24 equations) ----
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

; ---- Deduplication via hash-space ----
!(bind! &seen (new-space hash))

(= (try-add-unique $state)
   (case (collapse (match &seen (seen $state) found))
     ((() (let () (add-atom &seen (seen $state)) $state)))))

; ---- BFS capped at 5000 steps ----
(= (enqueue-all $q ()) ())
(= (enqueue-all $q $items)
   (let ($head $tail) (decons-atom $items)
     (let () (space-push $q $head)
       (enqueue-all $q $tail))))

(= (bfs-step $q $n)
   (if (or (== (space-len $q) 0) (>= $n 5000))
       $n
       (let* (($state (space-pop $q))
              ($neighbors (collapse
                (let $snew (move $state $dir)
                  (try-add-unique $snew))))
              (() (enqueue-all $q $neighbors)))
         (bfs-step $q (+ $n 1)))))

(= (bfs-all $start)
   (let* ((() (add-atom &seen (seen $start)))
          ($q (new-space queue))
          (() (space-push $q $start)))
     (bfs-step $q 0)))

!(bfs-all (___ 1 2 3 4 5 6 7 8))
