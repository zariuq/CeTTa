; Typed facts and durable application receipts over the native byte store.
; Identities are complete CAV1 values, not hashes or database row numbers.
; This first interface supports explicit point observations. Applications must
; include every influencing read; range/matching queries remain separate.
!(import! &self (library store))
!(import! &self (library str))

; Protocol records are data constructors, not patterns to instantiate.
(= (knowledge:headed? $value)
  (if (== (get-metatype $value) Expression)
    (if (> (size-atom $value) 0)
      (not (== (get-metatype (car-atom $value)) Variable)) False) False))
(= (knowledge:tagged? $value $tag $size)
  (if (knowledge:headed? $value)
    (and (== (size-atom $value) $size) (== (car-atom $value) $tag)) False))

(= (knowledge:ground-identity? $identity)
  (case (get-metatype $identity)
    ((Variable False)
     (Expression (knowledge:ground-items? $identity))
     ($other True))))
(= (knowledge:ground-items? $items)
  (if (== $items ()) True
    (if (knowledge:ground-identity? (car-atom $items)) (knowledge:ground-items? (cdr-atom $items)) False)))

(= (knowledge:address $prefix $identity)
  (if (knowledge:ground-identity? $identity)
    (case (store:encode $identity)
      (((StoreEncoded $bytes) (KnowledgeAddressV1 (str:concat $prefix $bytes)))
       ($error $error)))
    (KnowledgeRefusedV1 nonground-identity)))
(= (knowledge:fact-address $identity) (knowledge:address "knowledge/fact/v1/" $identity))
(= (knowledge:receipt-address $identity) (knowledge:address "knowledge/receipt/v1/" $identity))

(= (knowledge:append $left $right)
  (if (== $left ()) $right
    (cons-atom (car-atom $left) (knowledge:append (cdr-atom $left) $right))))

(= (knowledge:same $left $right)
  (case (store:encode $left)
    (((StoreEncoded $a)
      (case (store:encode $right)
        (((StoreEncoded $b) (KnowledgeEqualV1 (== $a $b)))
         ($error $error))))
     ($error $error))))

(= (knowledge:read $view $identity $maximum-bytes)
  (case (knowledge:fact-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (store:get $view $key $maximum-bytes)
        ((StoreAbsent KnowledgeAbsentV1)
         ((StoreValue $bytes)
          (case (store:decode $bytes)
            (((StoreDecoded (KnowledgeFactV1 $stored $value))
              (case (knowledge:same $identity $stored)
                (((KnowledgeEqualV1 True) (KnowledgePresentV1 $value))
                 ((KnowledgeEqualV1 False) (KnowledgeRefusedV1 fact-identity))
                 ($error $error))))
             ($other (KnowledgeRefusedV1 fact-format)))))
         ($error $error))))
     ($error $error))))

(= (knowledge:lookup $points $identity)
  (if (== $points ()) KnowledgeUnobservedV1
    (case (car-atom $points)
      (((KnowledgePointV1 $stored $value)
        (case (knowledge:same $identity $stored)
          (((KnowledgeEqualV1 True) $value)
           ((KnowledgeEqualV1 False) (knowledge:lookup (cdr-atom $points) $identity))
           ($error $error))))
       ($other (KnowledgeRefusedV1 observation-format))))))

(= (knowledge:observe $view $identities $maximum-bytes)
  (if (== (get-metatype $identities) Expression)
    (knowledge:observe-list $view $identities $maximum-bytes) (KnowledgeRefusedV1 identity-list)))
(= (knowledge:observe-list $view $identities $maximum-bytes)
  (if (== $identities ()) (KnowledgeObservedV1 () (reads))
    (let* (($identity (car-atom $identities)) ($tail (cdr-atom $identities)))
      (case (knowledge:read $view $identity $maximum-bytes)
        ((KnowledgeAbsentV1
          (knowledge:observe-next $view $identity KnowledgeAbsentV1 $tail $maximum-bytes))
         ((KnowledgePresentV1 $value)
          (knowledge:observe-next $view $identity (KnowledgePresentV1 $value) $tail $maximum-bytes))
         ($error $error))))))

(= (knowledge:observe-next $view $identity $value $tail $maximum-bytes)
  (case (knowledge:fact-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (knowledge:observe $view $tail $maximum-bytes)
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
(= (knowledge:observe-bound $view $identities $maximum-bytes)
  (case (knowledge:observe $view $identities $maximum-bytes)
    (((KnowledgeObservedV1 $points $reads) (KnowledgeObservedOnV2 $view $points $reads))
     ($error $error))))
(= (knowledge:scopes-on $view $observation)
  (if (knowledge:tagged? $observation KnowledgeObservedOnV2 4)
    (knowledge:scopes-value $view $observation)
    (KnowledgeRefusedV1 bound-observation-required)))
(= (knowledge:scopes-value $view $observation)
  (case $observation
    (((KnowledgeObservedOnV2 $captured $points $reads)
      (if (== $view $captured) (KnowledgeReadScopesV2 $reads)
        (KnowledgeRefusedV1 observation-view)))
     ($other (KnowledgeRefusedV1 bound-observation-required)))))

; Prepare every edit before publication. Ordered edits use the last value for
; an identity; removing an absent identity is a harmless local no-op.
(= (knowledge:prepare $changes)
  (if (== (get-metatype $changes) Expression)
    (knowledge:prepare-list $changes) (KnowledgeRefusedV1 mutation-list)))
(= (knowledge:prepare-list $changes)
  (if (== $changes ()) (KnowledgeDeltaV1 (ops) (writes))
    (if (knowledge:headed? (car-atom $changes)) (knowledge:prepare-item $changes)
      (KnowledgeRefusedV1 mutation-format))))
(= (knowledge:prepare-item $changes)
    (case (car-atom $changes)
      (((KnowledgePutV1 $identity $value)
        (case (store:encode (KnowledgeFactV1 $identity $value))
          (((StoreEncoded $bytes)
            (knowledge:prepare-one $identity (KnowledgeBodyV1 $bytes) (cdr-atom $changes)))
           ($error $error))))
       ((KnowledgeRemoveV1 $identity)
        (knowledge:prepare-one $identity KnowledgeDeleteV1 (cdr-atom $changes)))
       ($other (KnowledgeRefusedV1 mutation-format)))))

(= (knowledge:prepare-one $identity $body $remaining)
  (case (knowledge:fact-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (knowledge:prepare $remaining)
        (((KnowledgeDeltaV1 $operations $writes)
          (let* (($operation (case $body
                    (((KnowledgeBodyV1 $bytes) (put $key $bytes))
                     (KnowledgeDeleteV1 (remove $key)))))
                 ($next (cons-atom ops (cons-atom $operation (cdr-atom $operations))))
                 ($scopes (cons-atom writes (cons-atom (point $key) (cdr-atom $writes)))))
            (KnowledgeDeltaV1 $next $scopes)))
         ($error $error))))
     ($error $error))))

(= (knowledge:receipt $view $identity $command $maximum-bytes)
  (case (knowledge:receipt-address $identity)
    (((KnowledgeAddressV1 $key)
      (case (store:get $view $key $maximum-bytes)
        ((StoreAbsent KnowledgeReceiptAbsentV1)
         ((StoreValue $bytes)
          (case (store:decode $bytes)
            (((StoreDecoded (KnowledgeReceiptV1 $stored $original $answer))
              (case (knowledge:same $identity $stored)
                (((KnowledgeEqualV1 True)
                  (case (knowledge:same $command $original)
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
(= (knowledge:publish $owner $view $reads $identity $command $answer $changes $maximum-bytes)
  (case (knowledge:receipt $view $identity $command $maximum-bytes)
    ((KnowledgeReceiptAbsentV1
      (case (knowledge:prepare $changes)
        (((KnowledgeDeltaV1 $operations $writes)
          (knowledge:publish-ready $owner $view $reads $identity $command $answer $operations $writes))
         ($error $error))))
     ($replay-or-error (knowledge:replay-on $owner $view $replay-or-error)))))

; A receipt lookup is a historical read. Returning it from a publication API
; additionally requires a live publisher for this same store domain/epoch.
; A later actual publication still validates all influencing scopes atomically.
(= (knowledge:replay-on $owner $view $result)
  (case $result
    (((KnowledgeReplayedV1 $answer)
      (case (store:check-view $owner $view)
        ((StoreViewValid (KnowledgeReplayedV1 $answer)) ($error $error))))
     ($other $other))))

(= (knowledge:publish-ready $owner $view $reads $identity $command $answer $operations $writes)
  (case (knowledge:receipt-address $identity)
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
(= (knowledge:resolve $owner $identity $command $maximum-bytes)
  (case (store:recover $owner)
    (((StoreSnapshot $view $epoch $revision)
      (let* (($answer (case (knowledge:receipt $view $identity $command $maximum-bytes)
                       ((KnowledgeReceiptAbsentV1 (KnowledgeUnrecordedV1 $epoch $revision))
                        ((KnowledgeReplayedV1 $result) (KnowledgeResolvedV1 $result $epoch $revision))
                        ($error $error))))
             ($closed (store:close $view))) $answer))
     ($error $error))))
