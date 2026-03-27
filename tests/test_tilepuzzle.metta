; ============================================================================
; 8-Puzzle BFS Solver — adapted from PeTTa tilepuzzle.metta
; ============================================================================
; Original: https://github.com/trueagi-io/PeTTa/blob/main/examples/tilepuzzle.metta
; Adapted for HE/CeTTa: queue-space instead of lib_datastructures,
; case(collapse(match)) instead of PeTTa's fail, foldl→foldl-atom.
; ============================================================================

; ---- Move rules (24 equations for 3x3 tile puzzle) -----------------------
; ___ represents the blank tile. Each equation moves blank in one direction.

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

; ---- Deduplication via hash-space ------------------------------------------
; Use case on collapse(match) to produce zero results for duplicates.
; PeTTa uses (fail) which is a Prolog builtin; HE has no fail — instead,
; a case with no matching branch produces no results.
; The visited-set workload is exact membership, so hash-space is the honest
; structured-space fit here.
!(bind! &seen (new-space hash))

(= (try-add-unique $state)
   (case (collapse (match &seen (seen $state) found))
     ((() (let () (add-atom &seen (seen $state)) $state)))))

; ---- BFS ------------------------------------------------------------------

(= (enqueue-all $q ()) ())
(= (enqueue-all $q $items)
   (let ($head $tail) (decons-atom $items)
     (let () (space-push $q $head)
       (enqueue-all $q $tail))))

(= (bfs-step $q $n)
   (if (== (space-len $q) 0)
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

; ---- Run -------------------------------------------------------------------
!(println! "8-puzzle BFS: counting all reachable states from solved position...")
!(bfs-all (___ 1 2 3 4 5 6 7 8))
; Expected: 181441 (all reachable states of the 8-puzzle)
