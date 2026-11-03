; Ordered candidate selection on inert provider snapshots.
; Helpers live in the gslt:ordered: namespace.
; gslt:ordered:concatenate appends candidate lists and is not a scanner trace.
; gslt:ordered:first-success and gslt:ordered:complete-scan are the programs.
; A snapshot is a list of (gslt:ordered:row key occurrences). A missing key is empty.
; Occurrence identity and payload stay separate fields.
; A pause follows the retained prefix: an empty candidate list is already
; finished, an empty read stays pending on the rest, and a nonempty read
; closes over that first occurrence.
; Controls that call these helpers live in tests/petta/gslt_ordered_decision.metta.

(= (gslt:ordered:row-key (gslt:ordered:row $key $occurrences)) $key)
(= (gslt:ordered:row-occurrences (gslt:ordered:row $key $occurrences)) $occurrences)
(= (gslt:ordered:occ-id (gslt:ordered:occ $id $payload)) $id)
(= (gslt:ordered:occ-payload (gslt:ordered:occ $id $payload)) $payload)
(= (gslt:ordered:read-key (gslt:ordered:read $key $occurrences)) $key)
(= (gslt:ordered:read-occurrences (gslt:ordered:read $key $occurrences)) $occurrences)
(= (gslt:ordered:selected-answer (gslt:ordered:selected $answer $reads)) $answer)
(= (gslt:ordered:selected-reads (gslt:ordered:selected $answer $reads)) $reads)
(= (gslt:ordered:paused-reads (gslt:ordered:paused $reads $residual)) $reads)
(= (gslt:ordered:paused-residual (gslt:ordered:paused $reads $residual)) $residual)
(= (gslt:ordered:plan-id (gslt:ordered:plan $id $store $reads $residual)) $id)
(= (gslt:ordered:plan-store (gslt:ordered:plan $id $store $reads $residual)) $store)
(= (gslt:ordered:plan-reads (gslt:ordered:plan $id $store $reads $residual)) $reads)
(= (gslt:ordered:plan-residual (gslt:ordered:plan $id $store $reads $residual)) $residual)

(= (gslt:ordered:lookup $rows $key)
   (if (== $rows ())
       ()
       (let ($row $rest) (decons-atom $rows)
         (if (== (gslt:ordered:row-key $row) $key)
             (gslt:ordered:row-occurrences $row)
             (gslt:ordered:lookup $rest $key)))))

(= (gslt:ordered:concatenate $store $candidates)
   (if (== $candidates ())
       ()
       (let ($key $rest) (decons-atom $candidates)
         (union-atom (gslt:ordered:lookup $store $key)
                     (gslt:ordered:concatenate $store $rest)))))

(= (gslt:ordered:concatenate-head $store $candidates)
   (let $joined (gslt:ordered:concatenate $store $candidates)
     (if (== $joined ())
         none
         (car-atom $joined))))

(= (gslt:ordered:first-success $store $candidates)
   (if (== $candidates ())
       (gslt:ordered:selected none ())
       (let ($key $rest) (decons-atom $candidates)
         (let $here (gslt:ordered:lookup $store $key)
           (if (== $here ())
               (let $tail (gslt:ordered:first-success $store $rest)
                 (gslt:ordered:selected (gslt:ordered:selected-answer $tail)
                   (cons-atom (gslt:ordered:read $key $here)
                              (gslt:ordered:selected-reads $tail))))
               (gslt:ordered:selected (car-atom $here)
                 (cons-atom (gslt:ordered:read $key $here) ())))))))

(= (gslt:ordered:complete-scan $store $candidates)
   (if (== $candidates ())
       (gslt:ordered:selected () ())
       (let ($key $rest) (decons-atom $candidates)
         (let* (($here (gslt:ordered:lookup $store $key))
                ($tail (gslt:ordered:complete-scan $store $rest)))
           (gslt:ordered:selected (union-atom $here (gslt:ordered:selected-answer $tail))
             (cons-atom (gslt:ordered:read $key $here)
                        (gslt:ordered:selected-reads $tail)))))))

(= (gslt:ordered:at-most-one $occurrences)
   (if (== $occurrences ())
       True
       (let $tail (cdr-atom $occurrences)
         (if (== $tail ())
             True
             False))))

(= (gslt:ordered:at-most-one-scan $store $candidates)
   (gslt:ordered:at-most-one
     (gslt:ordered:selected-answer (gslt:ordered:complete-scan $store $candidates))))

(= (gslt:ordered:reads-agree $store $reads)
   (if (== $reads ())
       True
       (let ($one $rest) (decons-atom $reads)
         (if (== (gslt:ordered:lookup $store (gslt:ordered:read-key $one))
                 (gslt:ordered:read-occurrences $one))
             (gslt:ordered:reads-agree $store $rest)
             False))))

(= (gslt:ordered:pause-after-one $store $candidates)
   (if (== $candidates ())
       (gslt:ordered:paused () (gslt:ordered:residual-done none))
       (let ($key $rest) (decons-atom $candidates)
         (let $here (gslt:ordered:lookup $store $key)
           (if (== $here ())
               (gslt:ordered:paused
                 (cons-atom (gslt:ordered:read $key $here) ())
                 (gslt:ordered:residual-pending $rest))
               (gslt:ordered:paused
                 (cons-atom (gslt:ordered:read $key $here) ())
                 (gslt:ordered:residual-done (car-atom $here))))))))

(= (gslt:ordered:resume-residual $store (gslt:ordered:residual-done $answer))
   (gslt:ordered:selected $answer ()))

(= (gslt:ordered:resume-residual $store (gslt:ordered:residual-pending $candidates))
   (gslt:ordered:first-success $store $candidates))

(= (gslt:ordered:resume-validated $store $paused)
   (if (gslt:ordered:reads-agree $store (gslt:ordered:paused-reads $paused))
       (gslt:ordered:resume-residual $store (gslt:ordered:paused-residual $paused))
       refused))

(= (gslt:ordered:accept-plan $expected $plan)
   (if (== $expected (gslt:ordered:plan-id $plan))
       (gslt:ordered:resume-validated (gslt:ordered:plan-store $plan)
         (gslt:ordered:paused (gslt:ordered:plan-reads $plan)
                              (gslt:ordered:plan-residual $plan)))
       refused))
