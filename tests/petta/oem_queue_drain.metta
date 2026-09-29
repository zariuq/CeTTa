; A functional queue loaded and drained, as lib_datastructures defines it.
; Its two dequeue equations differ below their first test, in the queue's
; outgoing side: a call with a nonempty side can match only the first, and
; keeps no alternative for the second while the drain runs on.

(= (enqueue $E (queue $In $Out $N))
   (queue (cons $E $In) $Out (+ $N 1)))
(= (dequeue $E (queue $In (cons $E $Out) $N))
   (queue $In $Out (- $N 1)))
(= (dequeue $E (queue $In () $N))
   (let (cons $E $R) (reverse $In)
        (queue () $R (- $N 1))))
(= (empty-queue) (queue () () 0))

(= (load-queue $n $q) (if (== $n 0) $q (load-queue (- $n 1) (enqueue $n $q))))
(= (drain-queue $q $sum)
   (let (queue $a $b $size) $q
     (if (== $size 0)
         $sum
         (let $q2 (dequeue $item $q) (drain-queue $q2 (+ $sum $item))))))

!(drain-queue (load-queue 10 (empty-queue)) 0)
!(drain-queue (load-queue 200000 (empty-queue)) 0)
