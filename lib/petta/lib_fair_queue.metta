; Cost-priority selection with an oldest-entry turn after seven cost turns.
; This ordinary reference queue matches FairQueue.lean. Entries have unique
; increasing tickets; equal payloads remain separate occurrences.
; Offer/take preserve a valid queue; check decoded data with valid? first.
!(import! &self (library binary))

(= (fair-queue:empty) (Fair:Queue 0 0 ()))

(= (fair-queue:offer $queue $cost $value)
   (case $queue
     (((Fair:Queue $phase $next $entries)
       (if (binary:unsigned? $cost)
         (if (binary:unsigned? (+ $next 1))
           (let $pending (union-atom $entries ((Fair:Entry $next $cost $value)))
             (Fair:Queue $phase (+ $next 1) $pending))
           (Fair:Fault TicketCapacity))
         (Fair:Fault InvalidCost)))
      ($other (Fair:Fault InvalidQueue)))))

(= (fair-queue:minimum $head $tail)
   (if (== $tail ()) (Fair:Minimum $head ())
     (let ($next $rest) (decons-atom $tail)
       (case (fair-queue:minimum $next $rest)
         (((Fair:Minimum $best $pending)
           (case ($head $best)
             ((((Fair:Entry $id $cost $value) (Fair:Entry $best-id $best-cost $best-value))
               (if (<= $cost $best-cost) (Fair:Minimum $head $tail)
                 (let $remaining (cons-atom $head $pending) (Fair:Minimum $best $remaining))))))))))))

(= (fair-queue:take $queue)
   (case $queue
     (((Fair:Queue $phase $next $entries)
       (if (== $entries ()) Fair:Empty
         (let ($head $tail) (decons-atom $entries)
           (let $choice (if (== $phase 7) (Fair:Minimum $head $tail)
                           (fair-queue:minimum $head $tail))
             (case $choice
               (((Fair:Minimum $selected $pending)
                 (let $later-phase (if (== $phase 7) 0 (+ $phase 1))
                   (Fair:Taken $selected (Fair:Queue $later-phase $next $pending))))))))))
      ($other (Fair:Fault InvalidQueue)))))

(= (fair-queue:entries? $entries $previous $next)
   (if (== $entries ()) True
     (if (== (get-metatype $entries) Expression)
       (let ($entry $tail) (decons-atom $entries)
         (case $entry
           (((Fair:Entry $id $cost $value)
             (and (binary:unsigned? $id)
               (and (binary:unsigned? $cost)
                 (and (< $previous $id)
                   (and (< $id $next) (fair-queue:entries? $tail $id $next))))))
            ($other False)))) False)))

(= (fair-queue:valid? $queue)
   (case $queue
     (((Fair:Queue $phase $next $entries)
       (and (binary:unsigned? $phase)
         (and (< $phase 8)
           (and (binary:unsigned? $next) (fair-queue:entries? $entries -1 $next)))))
      ($other False))))
