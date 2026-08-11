; PeTTa functional queue and exact-lookup helpers used by runtime witnesses.

(= (enqueue $E (queue $In $Out $N))
   (queue (cons $E $In) $Out (+ $N 1)))

(= (dequeue $E (queue $In (cons $E $Out) $N))
   (queue $In $Out (- $N 1)))
(= (dequeue $E (queue $In () $N))
   (let (cons $E $R) (reverse $In)
        (queue () $R (- $N 1))))

(= (empty-queue) (queue () () 0))

(= (add-unique-or-fail $space $Expression)
   (let $st (s (repra $Expression))
        (if (== () (collapse (once (match $space $st True))))
            (add-atom $space $st)
            (empty))))
