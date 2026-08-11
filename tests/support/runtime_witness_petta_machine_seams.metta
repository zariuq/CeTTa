; Bounded PeTTa witnesses which separate tail-call, collapse, and growing
; persistent-queue costs without changing the language surface under test.

!(import! &self runtime_witness_petta_lib_datastructures)
!(import! &self system)

(= (tail-loop $n $limit)
   (if (== $n $limit)
       $n
       (tail-loop (+ $n 1) $limit)))

(= (collapse-loop $n $limit)
   (if (== $n $limit)
       $n
       (let $items (collapse (superpose (alpha beta gamma)))
            (if (== (size-atom $items) 3)
                (collapse-loop (+ $n 1) $limit)
                (empty)))))

(= (queue-loop $queue $n $limit)
   (if (== $n $limit)
       $n
       (let* (($rest (once (dequeue $_ $queue)))
              ($one-more
               (enqueue
                (state 1 2 3 4 5 6 7 8 9)
                (enqueue
                 (state 1 2 3 4 5 6 7 8 9)
                 $rest)))
             ($next (+ $n 1)))
             (queue-loop $one-more $next $limit))))

(= (queue-steady-loop $queue $n $limit)
   (if (== $n $limit)
       $n
       (let* (($rest (once (dequeue $_ $queue)))
              ($same-size
               (enqueue
                (state 1 2 3 4 5 6 7 8 9)
                $rest))
              ($next (+ $n 1)))
             (queue-steady-loop $same-size $next $limit))))

(= (run-witness tail $limit)
   (tail-loop 0 $limit))
(= (run-witness collapse $limit)
   (collapse-loop 0 $limit))
(= (run-witness queue $limit)
   (queue-loop
    (enqueue (state 1 2 3 4 5 6 7 8 9) (empty-queue))
    0
    $limit))
(= (run-witness queue-steady $limit)
   (queue-steady-loop
    (enqueue (state 1 2 3 4 5 6 7 8 9) (empty-queue))
    0
    $limit))

!(reset-runtime-stats!)
!(run-witness (system:arg 0) (sread (system:arg 1)))
