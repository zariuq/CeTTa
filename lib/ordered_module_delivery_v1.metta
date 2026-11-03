; Ordered publication cursor. The caller supplies its actual session lineage.
; Captured receipts are a prefix; count, lineage and prefix are checked before
; any new work is licensed. Work budget and answer limit are independent.

(: ordered:delivery:take (-> Atom Atom %Undefined%))
(= (ordered:delivery:take $n $entries)
   (if (== $n 0) LNil
     (case $entries
       ((LNil LNil)
        ((LCons $entry $rest)
         (let $previous (- $n 1)
           (let $tail (ordered:delivery:take $previous $rest)
             (LCons $entry $tail))))))))

(: ordered:delivery:drop (-> Atom Atom %Undefined%))
(= (ordered:delivery:drop $n $entries)
   (if (== $n 0) $entries
     (case $entries
       ((LNil LNil)
        ((LCons $entry $rest)
         (let $previous (- $n 1)
           (ordered:delivery:drop $previous $rest)))))))

(: ordered:delivery:valid (-> Atom Atom Atom %Undefined%))
(= (ordered:delivery:valid $lineage $cursor $publication)
   (case ($cursor $publication)
     ((((OrderedDeliveryCursorV1 $owner $next $captured)
        (ProgramPublication $entries $status))
       (if (== $lineage $owner)
         (let $count (gslt:automata:length $captured)
           (if (== $next $count)
             (let $prefix (ordered:delivery:take $next $entries)
               (== $captured $prefix)) False)) False))
      ($malformed False))))

(: ordered:delivery:advance (-> Atom Atom Atom Atom Atom %Undefined%))
(= (ordered:delivery:advance $lineage $cursor $limit $publication $state)
   (if (ordered:delivery:valid $lineage $cursor $publication)
     (case ($cursor $publication)
       ((((OrderedDeliveryCursorV1 $owner $next $captured)
          (ProgramPublication $entries $status))
         (let $remaining (ordered:delivery:drop $next $entries)
           (let $selected (ordered:delivery:take $limit $remaining)
             (let* (($count (gslt:automata:length $selected))
                    ($updated (gslt:automata:append $captured $selected))
                    ($cursorNext (+ $next $count))
                    ($pending (ordered:delivery:drop $count $remaining)))
               (OrderedDeliveryV1
                 (OrderedDeliveryCursorV1 $owner $cursorNext $updated)
                 $selected $pending $status $state)))))))
     (OrderedDeliveryRefusedV1 $cursor $state)))
