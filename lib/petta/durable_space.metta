; Durable atom collections, queried through ordinary Space operations.
; Native store capabilities own authority; collection names and occurrence
; references are identifiers only. Each commit validates the whole revision.
!(import! &self "knowledge.metta")
!(import! &self (library list))

; The CAV1 codec refuses packets beyond one MiB before any publication.
(= (durable-space:wire-limit) 1048576)
(: durable-space:create (-> %Undefined% %Undefined% Atom Atom %Undefined%))
(: durable-space:capture (-> %Undefined% Atom %Undefined%))
(: durable-space:cursor (-> %Undefined% Atom Atom Atom %Undefined%))
(= (durable-space:metadata $collection) (knowledge:address "space/meta/v1/" $collection))
(= (durable-space:prefix $collection)
  (case (knowledge:address "space/rows/v1/" $collection)
    (((KnowledgeAddressV1 $key) (KnowledgeAddressV1 (str:concat $key "/"))) ($error $error))))

(= (durable-space:capture $view $collection)
  (case (durable-space:metadata $collection)
    (((KnowledgeAddressV1 $key) (store:space-view $view $key)) ($error $error))))

(= (durable-space:cursor $view $collection $pattern $template)
  (case (durable-space:metadata $collection)
    (((KnowledgeAddressV1 $key) (store:space-query-open $view $key $pattern $template)) ($error $error))))

; A new collection must have neither metadata nor orphaned rows. The scan
; reads at most one key at/after the prefix and closes on every outcome.
(= (durable-space:empty-prefix? $view $prefix)
  (case (store:scan-open $view $prefix NoBound)
    (((StoreScan $scan)
      (let* (($first (store:scan-next $scan 0)) ($_ (store:close $scan)))
        (case $first
          ((StoreEnd True)
           ((StoreKey $key $length $next) (not (str:starts-with $key $prefix)))
           ($error $error)))))
     ($error $error))))

(= (durable-space:create $owner $view $collection $request)
  (let $command (SpaceCreateV1 $collection)
    (case (knowledge:receipt $view $request $command (durable-space:wire-limit))
      ((KnowledgeReceiptAbsentV1
        (case (durable-space:metadata $collection)
          (((KnowledgeAddressV1 $key)
            (case (durable-space:prefix $collection)
              (((KnowledgeAddressV1 $prefix)
                (case (store:get $view $key 0)
                  ((StoreAbsent
                    (case (durable-space:empty-prefix? $view $prefix)
                      ((True
                        (case (store:encode (StoreSpaceV1 $prefix 0))
                          (((StoreEncoded $body)
                            (knowledge:publish-whole-ready $owner $view $request $command
                              (SpaceCreatedV1 $collection) (ops (put $key $body))))
                           ($error $error))))
                       (False (SpaceRefusedV1 orphaned-occurrences))
                       ($error $error))))
                   ((StoreValue $body) (SpaceRefusedV1 collection-exists))
                   ((StoreTooLarge $length) (SpaceRefusedV1 collection-exists))
                   ($error $error))))
               ($error $error))))
           ($error $error))))
       ($result (knowledge:replay-on $owner $view $result))))))

(= (durable-space:count $view $key $prefix)
  (case (store:get $view $key (durable-space:wire-limit))
    (((StoreValue $body)
      (case (store:decode $body)
        (((StoreDecoded $record)
          (if (knowledge:tagged? $record StoreSpaceV1 3)
            (let (StoreSpaceV1 $stored $count) $record
              (case (store:key-i64 $count)
                (((StoreKeyBytes $integer)
                  (if (and (== $stored $prefix) (>= $count 0)) (SpaceCountV1 $count)
                    (SpaceRefusedV1 metadata-format)))
                 ($other (SpaceRefusedV1 metadata-format)))))
            (SpaceRefusedV1 metadata-format)))
         ($error $error))))
     (StoreAbsent (SpaceRefusedV1 collection-absent))
     ($error $error))))

(= (durable-space:exists $view $key)
  (case (store:get $view $key 0)
    ((StoreAbsent (SpaceExistsV1 False))
     ((StoreValue $body) (SpaceExistsV1 True))
     ((StoreTooLarge $length) (SpaceExistsV1 True))
     ($error $error))))

(= (durable-space:reference-key $reference $epoch $prefix)
  (if (knowledge:tagged? $reference StoreOccurrenceV1 3)
    (let (StoreOccurrenceV1 $stored $key) $reference
      (if (and (== (get-type $key) String) (== $stored $epoch))
        (if (str:starts-with $key $prefix) (KnowledgeAddressV1 $key)
          (SpaceRefusedV1 occurrence-collection))
        (SpaceRefusedV1 occurrence-reference)))
    (SpaceRefusedV1 occurrence-reference)))

; Insertion identities include the durable request id. A successful retry
; resolves its immutable receipt, including after deletion; it cannot reuse
; the same occurrence for a new insertion. Distinct requests may store equal
; payloads. Updates/deletes use the stable reference returned by insertion.
(= (durable-space:edit $view $epoch $prefix $request $change)
  (if (knowledge:headed? $change)
    (case $change
      (((SpaceInsertV1 $local $atom)
        (case (knowledge:address $prefix (SpaceOccurrenceKeyV1 $request $local))
          (((KnowledgeAddressV1 $key)
            (case (durable-space:exists $view $key)
              (((SpaceExistsV1 False)
                (case (store:encode $atom)
                  (((StoreEncoded $body)
                    (SpaceEditV1 $key 1 (put $key $body) (CreatedV1 (StoreOccurrenceV1 $epoch $key))))
                   ($error $error))))
               ((SpaceExistsV1 True) (SpaceRefusedV1 occurrence-exists))
               ($error $error))))
           ($error $error))))
       ((SpaceUpdateV1 $reference $atom)
        (case (store:encode $atom)
          (((StoreEncoded $body) (durable-space:existing-edit $view $epoch $prefix $reference (SpaceBodyV1 $body)))
           ($error $error))))
       ((SpaceDeleteV1 $reference)
        (durable-space:existing-edit $view $epoch $prefix $reference SpaceDeleteBodyV1))
       ($bad (SpaceRefusedV1 edit-format))))
    (SpaceRefusedV1 edit-format)))

(= (durable-space:existing-edit $view $epoch $prefix $reference $body)
  (case (durable-space:reference-key $reference $epoch $prefix)
    (((KnowledgeAddressV1 $key)
      (case (durable-space:exists $view $key)
        (((SpaceExistsV1 True)
          (case $body
            (((SpaceBodyV1 $bytes) (SpaceEditV1 $key 0 (put $key $bytes) NoCreatedV1))
             (SpaceDeleteBodyV1 (SpaceEditV1 $key -1 (remove $key) NoCreatedV1)))))
         ((SpaceExistsV1 False) (SpaceRefusedV1 occurrence-absent))
         ($error $error))))
     ($error $error))))

(= (durable-space:seen? $key $keys)
  (if (== $keys ()) False
    (if (== $key (car-atom $keys)) True (durable-space:seen? $key (cdr-atom $keys)))))
(= (durable-space:stage $view $epoch $prefix $request $changes $seen $count $ops $created)
  (if (== (get-metatype $changes) Expression)
    (if (== $changes ()) (SpacePreparedV1 $count (list:reverse $ops) (list:reverse $created))
      (case (durable-space:edit $view $epoch $prefix $request (car-atom $changes))
        (((SpaceEditV1 $key $delta $operation $new)
          (if (durable-space:seen? $key $seen) (SpaceRefusedV1 repeated-occurrence)
            (if (< (+ $count $delta) 0) (SpaceRefusedV1 metadata-count)
              (durable-space:stage $view $epoch $prefix $request (cdr-atom $changes)
                (cons-atom $key $seen) (+ $count $delta) (cons-atom $operation $ops)
                (case $new (((CreatedV1 $reference) (cons-atom $reference $created)) (NoCreatedV1 $created)))))))
         ($error $error))))
    (SpaceRefusedV1 edit-list)))

; CHANGES is data, including quoted rules, callable expressions and open atoms.
(= (durable-space:publish $owner $view $request $command $key $prefix $next $ops $created)
  (case (store:key-i64 $next)
    (((StoreKeyBytes $integer)
      (case (store:encode (StoreSpaceV1 $prefix $next))
        (((StoreEncoded $body)
          (knowledge:publish-whole-ready $owner $view $request $command
            (SpaceAppliedV1 $created) (cons-atom ops (cons-atom (put $key $body) $ops))))
         ($error $error))))
     ($other (SpaceRefusedV1 metadata-count)))))

(: durable-space:commit (-> %Undefined% %Undefined% Atom Atom Atom %Undefined%))
(= (durable-space:commit $owner $view $collection $request $changes)
  (let $command (SpaceCommitV1 $collection $changes)
    (case (knowledge:receipt $view $request $command (durable-space:wire-limit))
      ((KnowledgeReceiptAbsentV1
        (case (durable-space:metadata $collection)
          (((KnowledgeAddressV1 $key)
            (case (durable-space:prefix $collection)
              (((KnowledgeAddressV1 $prefix)
                (case (durable-space:count $view $key $prefix)
                  (((SpaceCountV1 $count)
                    (case (store:view-info $view)
                      (((StoreViewInfo $epoch $revision)
                        (case (durable-space:stage $view $epoch $prefix $request $changes () $count () ())
                          (((SpacePreparedV1 $next $ops $created)
                            (durable-space:publish $owner $view $request $command $key $prefix $next $ops $created))
                           ($error $error))))
                       ($error $error))))
                   ($error $error))))
               ($error $error))))
           ($error $error))))
       ($result (knowledge:replay-on $owner $view $result))))))
