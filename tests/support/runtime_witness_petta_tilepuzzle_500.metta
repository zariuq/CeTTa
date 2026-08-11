; Bounded PeTTa 8-puzzle probe for search-machine profiling.

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

!(import! &self runtime_witness_petta_lib_datastructures)
!(import! &self system)

!(bind! &dup (new-space))

(= (bfs-loop-limited $q $n $limit)
   (if (== $n $limit)
       $n
       (let* (($q1 (once (dequeue $state $q)))
              ($neighbors
               (collapse
                (let* (($new-state (move $state $_))
                       ($added (add-unique-or-fail &dup $new-state)))
                      $new-state)))
              ($q2 (foldl enqueue $neighbors $q1))
              ($n1 (+ $n 1)))
             (bfs-loop-limited $q2 $n1 $limit))))

(= (bfs-all-limited $start $limit)
   (let* (($q (enqueue $start (empty-queue))))
         (bfs-loop-limited $q 0 $limit)))

(= (runtime-witness-limit)
   (if (system:has-args)
       (sread (system:arg 0))
       500))

!(reset-runtime-stats!)
!(bfs-all-limited
    (___ 1 2 3 4 5 6 7 8)
    (runtime-witness-limit))
