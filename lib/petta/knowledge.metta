; Typed facts and durable application receipts over the native byte store.
; Identities are complete CAV1 values, not hashes or database row numbers.
; This first interface supports explicit point observations. Applications must
; include every influencing read; range/matching queries remain separate.
!(import! &self (library store))
!(import! &self (library str))

(= (kb:ground-identity? $identity)
  (case (get-metatype $identity)
    ((Variable False)
     (Expression (kb:ground-items? $identity))
     ($other True))))
(= (kb:ground-items? $items)
  (if (== $items ()) True
    (if (kb:ground-identity? (car-atom $items)) (kb:ground-items? (cdr-atom $items)) False)))

(= (kb:address $prefix $identity)
  (if (kb:ground-identity? $identity)
    (case (store:encode $identity)
      (((StoreEncoded $bytes) (KnowledgeAddressV1 (str:concat $prefix $bytes)))
       ($error $error)))
    (KnowledgeRefusedV1 nonground-identity)))
(= (kb:fact-address $identity) (kb:address "knowledge/fact/v1/" $identity))
(= (kb:receipt-address $identity) (kb:address "knowledge/receipt/v1/" $identity))

(= (kb:append $left $right)
  (if (== $left ()) $right
    (cons-atom (car-atom $left) (kb:append (cdr-atom $left) $right))))

(= (kb:same $left $right)
  (case (store:encode $left)
    (((StoreEncoded $a)
      (case (store:encode $right)
        (((StoreEncoded $b) (KnowledgeEqualV1 (== $a $b)))
         ($error $error))))
     ($error $error))))

(= (kb:read $view $identity $maximum-bytes)
  (case (kb:fact-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (store:get $view $key $maximum-bytes)
        ((StoreAbsent KnowledgeAbsentV1)
         ((StoreValue $bytes)
          (case (store:decode $bytes)
            (((StoreDecoded (KnowledgeFactV1 $stored $value))
              (case (kb:same $identity $stored)
                (((KnowledgeEqualV1 True) (KnowledgePresentV1 $value))
                 ((KnowledgeEqualV1 False) (KnowledgeRefusedV1 fact-identity))
                 ($error $error))))
             ($other (KnowledgeRefusedV1 fact-format)))))
         ($error $error))))
     ($error $error))))

(= (kb:lookup $points $identity)
  (if (== $points ()) KnowledgeUnobservedV1
    (case (car-atom $points)
      (((KnowledgePointV1 $stored $value)
        (case (kb:same $identity $stored)
          (((KnowledgeEqualV1 True) $value)
           ((KnowledgeEqualV1 False) (kb:lookup (cdr-atom $points) $identity))
           ($error $error))))
       ($other (KnowledgeRefusedV1 observation-format))))))

(= (kb:observe $view $identities $maximum-bytes)
  (if (== $identities ()) (KnowledgeObservedV1 () (reads))
    (let* (($identity (car-atom $identities)) ($tail (cdr-atom $identities)))
      (case (kb:read $view $identity $maximum-bytes)
        ((KnowledgeAbsentV1
          (kb:observe-next $view $identity KnowledgeAbsentV1 $tail $maximum-bytes))
         ((KnowledgePresentV1 $value)
          (kb:observe-next $view $identity (KnowledgePresentV1 $value) $tail $maximum-bytes))
         ($error $error))))))

(= (kb:observe-next $view $identity $value $tail $maximum-bytes)
  (case (kb:fact-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (kb:observe $view $tail $maximum-bytes)
        (((KnowledgeObservedV1 $points $reads)
          (let* (($next (cons-atom (KnowledgePointV1 $identity $value) $points))
                 ($scopes (cons-atom reads (cons-atom (point $key) (cdr-atom $reads)))))
            (KnowledgeObservedV1 $next $scopes)))
         ($error $error))))
     ($error $error))))

; Keep a composable observation tied to its actual captured native view.
; The view is an opaque in-process handle, not a serializable capability.
; Consumers must keep this observation intact rather than copying read keys
; onto another view. Authentication and the decision still belong to a broker.
(= (kb:observe-bound $view $identities $maximum-bytes)
  (case (kb:observe $view $identities $maximum-bytes)
    (((KnowledgeObservedV1 $points $reads) (KnowledgeObservedOnV2 $view $points $reads))
     ($error $error))))
(= (kb:scopes-on $view $observation)
  (case $observation
    (((KnowledgeObservedOnV2 $captured $points $reads)
      (if (== $view $captured) (KnowledgeReadScopesV2 $reads)
        (KnowledgeRefusedV1 observation-view)))
     ($other (KnowledgeRefusedV1 bound-observation-required)))))

; Prepare every edit before publication. Ordered edits use the last value for
; an identity; removing an absent identity is a harmless local no-op.
(= (kb:prepare $changes)
  (if (== $changes ()) (KnowledgeDeltaV1 (ops) (writes))
    (case (car-atom $changes)
      (((KnowledgePutV1 $identity $value)
        (case (store:encode (KnowledgeFactV1 $identity $value))
          (((StoreEncoded $bytes)
            (kb:prepare-one $identity (KnowledgeBodyV1 $bytes) (cdr-atom $changes)))
           ($error $error))))
       ((KnowledgeRemoveV1 $identity)
        (kb:prepare-one $identity KnowledgeDeleteV1 (cdr-atom $changes)))
       ($other (KnowledgeRefusedV1 mutation-format))))))

(= (kb:prepare-one $identity $body $remaining)
  (case (kb:fact-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (kb:prepare $remaining)
        (((KnowledgeDeltaV1 $operations $writes)
          (let* (($operation (case $body
                    (((KnowledgeBodyV1 $bytes) (put $key $bytes))
                     (KnowledgeDeleteV1 (remove $key)))))
                 ($next (cons-atom ops (cons-atom $operation (cdr-atom $operations))))
                 ($scopes (cons-atom writes (cons-atom (point $key) (cdr-atom $writes)))))
            (KnowledgeDeltaV1 $next $scopes)))
         ($error $error))))
     ($error $error))))

(= (kb:receipt $view $identity $command $maximum-bytes)
  (case (kb:receipt-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (store:get $view $key $maximum-bytes)
        ((StoreAbsent KnowledgeReceiptAbsentV1)
         ((StoreValue $bytes)
          (case (store:decode $bytes)
            (((StoreDecoded (KnowledgeReceiptV1 $stored $original $answer))
              (case (kb:same $identity $stored)
                (((KnowledgeEqualV1 True)
                  (case (kb:same $command $original)
                    (((KnowledgeEqualV1 True) (KnowledgeReplayedV1 $answer))
                     ((KnowledgeEqualV1 False) (KnowledgeRefusedV1 receipt-collision))
                     ($error $error))))
                 ((KnowledgeEqualV1 False) (KnowledgeRefusedV1 receipt-identity))
                 ($error $error))))
             ($other (KnowledgeRefusedV1 receipt-format)))))
         ($error $error))))
     ($error $error))))

; A receipt is immutable through this interface. Retrying its identical
; command returns the original answer without applying the new edit list.
(= (kb:publish $owner $view $reads $identity $command $answer $changes $maximum-bytes)
  (case (kb:receipt $view $identity $command $maximum-bytes)
    ((KnowledgeReceiptAbsentV1
      (case (kb:prepare $changes)
        (((KnowledgeDeltaV1 $operations $writes)
          (kb:publish-ready $owner $view $reads $identity $command $answer $operations $writes))
         ($error $error))))
     ($replay-or-error $replay-or-error))))

(= (kb:publish-ready $owner $view $reads $identity $command $answer $operations $writes)
  (case (kb:receipt-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (store:encode (KnowledgeReceiptV1 $identity $command $answer))
        (((StoreEncoded $bytes)
          (let* (($all-reads (cons-atom reads (cons-atom (point $key) (cdr-atom $reads))))
                 ($all-writes (cons-atom writes (cons-atom (point $key) (cdr-atom $writes))))
                 ($all-operations (cons-atom ops (cons-atom (put $key $bytes) (cdr-atom $operations)))))
            (case (store:commit-scoped $owner $view $all-reads $all-writes $all-operations)
              (((StoreSnapshot $updated $epoch $revision)
                (let $closed (store:close $updated) (KnowledgeCommittedV1 $answer $epoch $revision)))
               ($error $error)))))
         ($error $error))))
     ($error $error))))

; Recovery establishes a fresh durable view before inspecting the receipt.
; Missing or unreadable evidence never grants authority for an external call.
(= (kb:resolve $owner $identity $command $maximum-bytes)
  (case (store:recover $owner)
    (((StoreSnapshot $view $epoch $revision)
      (let* (($answer (case (kb:receipt $view $identity $command $maximum-bytes)
                       ((KnowledgeReceiptAbsentV1 (KnowledgeUnrecordedV1 $epoch $revision))
                        ((KnowledgeReplayedV1 $result) (KnowledgeResolvedV1 $result $epoch $revision))
                        ($error $error))))
             ($closed (store:close $view))) $answer))
     ($error $error))))
