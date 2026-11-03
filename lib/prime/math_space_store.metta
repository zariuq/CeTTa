; Portable projects retain source and proof data. Restoring constructs a fresh
; theory and runs its native admission/checking interfaces before returning it.
(scope:profile lexical-fresh per-call reference)
!(import! &self "./math_space.metta")
!(import! &self fs)

(= (math:source-from-statement $name $statement)
   (case (get-metatype $statement)
     ((Expression
        (let ($head $parts) (decons-atom $statement)
          (case $head
            ((inductive
               (let $call (cons-atom hol:inductive (cons-atom $name $parts))
                 (let $code (math:code $call) (math:singleton $code))))
             (define
               (let $call (cons-atom hol:define (cons-atom $name $parts))
                 (let $code (math:code $call) (math:singleton $code))))
             ($other ())))))
      ($other ()))))
(= (math:source-row $row)
   (case $row
     (((hol:profile base) (Math:BaseProfile))
      ((hol:known $name $statement $origin $proof $deps $revision $signature)
        (math:source-from-statement $name $statement))
      ($other ()))))
(= (math:sources $project)
   (let* (($theory (math:theory $project)) ($rows (collapse (get-atoms $theory))))
     (foldl-atom $rows () $sources $row
       (union-atom $sources (math:source-row $row)))))

(= (math:research-row $row)
   (case $row
     (((Math:Goal $id $kind $statement $description) (math:singleton $row))
      ((Math:Candidate $id $goal $strategy $key $proof) (math:singleton $row))
      ((Math:Attempt $id $goal $strategy $key $proof $verdict $raw) (math:singleton $row))
      ((Math:PriorAttempt $id $goal $strategy $key $proof $verdict $raw) (math:singleton $row))
      ((Math:Note $id $text $sources) (math:singleton $row))
      ((Math:Link $source $relation $target) (math:singleton $row))
      ((Math:Archive $history) (math:singleton $row))
      ($other ()))))
(= (math:research $project)
   (let* (($records (math:records $project)) ($rows (collapse (get-atoms $records))))
     (foldl-atom $rows () $all $row (union-atom $all (math:research-row $row)))))
(= (math:publication-row $entry)
   (case $entry
     ((($index (Math:Publication $name $goal $kind $statement $proof))
        ((Math:Publication $name $goal $kind $statement $proof)))
      ($other ()))))
(= (math:publications $project)
   (let $history (math:history $project)
     (foldl-atom $history () $all $entry
       (union-atom $all (math:publication-row $entry)))))

; Source code, native context rows and research state have separate variable
; scopes. Sharing inside each artifact is preserved by one printer/reader run.
(= (math:encode-artifact $value)
   (let $text (repr $value)
     (if (== (std:get-type $text) String)
       (let $readback (parse $text)
         (if (=alpha $value $readback)
           (let $digest (str:sha256 $text) (Math:Artifact $digest $text))
           (Math:StoreFailed nonportable-artifact)))
       (Math:StoreFailed nonportable-artifact))))
(= (math:artifacts-ready $artifacts)
   (case $artifacts
     ((() True)
      ($some
        (let ($first $rest) (decons-atom $some)
          (case $first
            (((Math:Artifact $digest $text) (math:artifacts-ready $rest))
             ($other False))))))))
(= (math:bundle $project)
   (let* (($name (math:name $project))
          ($source-data (math:sources $project)) ($sources (math:encode-artifact $source-data))
          ($publication-data (math:publications $project)) ($publications (math:encode-artifact $publication-data))
          ($research-data (math:research $project)) ($research (math:encode-artifact $research-data))
          ($history-data (math:history $project)) ($history (math:encode-artifact $history-data))
          ($theory-data (math:theory-rows $project)) ($theory (math:encode-artifact $theory-data)))
     (if (math:artifacts-ready ($sources $publications $research $history $theory))
       (Math:Bundle 1 $name $sources $publications $research $history $theory)
       (Math:StoreFailed nonportable-data))))

; The alpha comparison includes the entire prefix, so different rows cannot
; independently rename one shared variable into two unrelated variables.
(= (math:align-candidate $first $rest $left-prefix $right-prefix $candidates $skipped)
   (case $candidates
     ((() False)
      ($some
        (let* ((($candidate $tail) (decons-atom $some))
               ($next-left (cons-atom $first $left-prefix))
               ($next-right (cons-atom $candidate $right-prefix)))
          (if (=alpha $next-left $next-right)
            (let $remaining (union-atom $skipped $tail)
              (if (math:align-rows $rest $remaining $next-left $next-right) True
                 (let $skip (union-atom $skipped (math:singleton $candidate))
                  (math:align-candidate $first $rest $left-prefix $right-prefix $tail $skip))))
            (let $skip (union-atom $skipped (math:singleton $candidate))
              (math:align-candidate $first $rest $left-prefix $right-prefix $tail $skip))))))))
(= (math:align-rows $left $right $left-prefix $right-prefix)
   (case $left
     ((() (== $right ()))
      ($some
        (let ($first $rest) (decons-atom $some)
          (math:align-candidate $first $rest $left-prefix $right-prefix $right ()))))))
(= (math:same-rows $left $right)
   (if (== (size-atom $left) (size-atom $right))
     (if (=alpha $left $right) True (math:align-rows $left $right () ()))
     False))

(= (math:json-keys $pairs)
   (case $pairs
     ((() ())
      ($some
        (let ((JsonPair $key $value) $rest) (decons-atom $some)
          (let $keys (math:json-keys $rest) (cons-atom $key $keys)))))))
(= (math:unique-json-object $value)
   (case $value
     (((JsonObject $pairs)
        (let* (($keys (math:json-keys $pairs)) ($unique (unique-atom $keys)))
          (== (size-atom $keys) (size-atom $unique))))
      ($other False))))
(= (math:pack-bundle $bundle)
   (let* (($text (repr $bundle))
          ($readback (parse $text)))
     (if (=alpha $bundle $readback)
       (let $digest (str:sha256 $text)
         (json:stringify (JsonObject
           ((JsonPair "schema" "prime.math-space.v1")
            (JsonPair "payload" $text) (JsonPair "sha256" $digest)))))
       (Math:StoreFailed nonportable-data))))
(= (math:serialize $project)
   (case (math:bundle $project)
     (((Math:Bundle 1 $name $sources $publications $research $history $theory)
        (let $bundle (Math:Bundle 1 $name $sources $publications $research $history $theory)
          (math:pack-bundle $bundle)))
      ($other $other))))

; Admission is repeated until the remaining dependency set stops shrinking.
; No assumed source order or fixed pass count becomes a language restriction.
(= (math:replay-source $project $source)
   (case $source
     ((Math:BaseProfile
        (let $theory (math:theory $project)
          (chain (add-atom $theory (hol:profile base)) $added
            (Established (base)))))
      ($other (math:declare $project $source)))))
(= (math:replay-source-step $project $state $source)
   (case $state
     (((Math:Pending $remaining $progress)
        (case (math:replay-source $project $source)
          (((Established $evidence)
             (let $next (+ $progress 1) (Math:Pending $remaining $next)))
           ($other
             (let $pending (union-atom $remaining (math:singleton $source))
               (Math:Pending $pending $progress))))))
      ($other $other))))
(= (math:replay-sources $project $sources)
   (case $sources
     ((() Math:SourcesReplayed)
      ($some
        (case (foldl-atom $some (Math:Pending () 0) $state $source
                (math:replay-source-step $project $state $source))
          (((Math:Pending () $progress) Math:SourcesReplayed)
           ((Math:Pending $remaining $progress)
             (if (> $progress 0) (math:replay-sources $project $remaining)
               (Math:RestoreFailed unresolved-declarations $remaining)))
           ($other $other)))))))
(= (math:replay-publication $project $entry $budget)
   (case $entry
     (((Math:Publication $name $goal $kind $statement $proof)
        (case (math:publish-native $project $name $kind $statement $proof $budget)
          (((Established $evidence)
             (case (math:record $project $entry)
               (($recorded Math:PublicationReplayed))))
           ($other (Math:RestoreFailed publication $name $other)))))
      ($other (Math:RestoreFailed malformed-publication)))))
(= (math:replay-publications $project $entries $budget)
   (case $entries
     ((() Math:PublicationsReplayed)
      ($some
        (let ($first $rest) (decons-atom $some)
          (case (math:replay-publication $project $first $budget)
            ((Math:PublicationReplayed (math:replay-publications $project $rest $budget))
             ($other $other))))))))

; A definition may use a previously published theorem, and that theorem may
; use earlier definitions. Replay both kinds until their shared dependency
; frontier closes; keeping them in separate phases loses valid projects.
(= (math:replay-publication-step $project $state $entry $budget)
   (case $state
     (((Math:PendingPublications $remaining $progress $failure)
        (case (math:replay-publication $project $entry $budget)
          ((Math:PublicationReplayed
             (let $next (+ $progress 1) (Math:PendingPublications $remaining $next $failure)))
           ($failed
             (let $pending (union-atom $remaining (math:singleton $entry))
               (Math:PendingPublications $pending $progress $failed))))))
      ($other $other))))
(= (math:replay-context $project $sources $publications $budget)
   (let $source-pass
          (foldl-atom $sources (Math:Pending () 0) $state $source
            (math:replay-source-step $project $state $source))
     (case $source-pass
       (((Math:Pending $pending-sources $source-progress)
          (let $publication-pass
                 (foldl-atom $publications (Math:PendingPublications () $source-progress none) $state $entry
                   (math:replay-publication-step $project $state $entry $budget))
            (case $publication-pass
              (((Math:PendingPublications $pending-publications $progress $failure)
                 (if (and (== $pending-sources ()) (== $pending-publications ()))
                   Math:ContextReplayed
                   (if (> $progress 0)
                     (math:replay-context $project $pending-sources $pending-publications $budget)
                     (if (== $pending-publications ())
                       (Math:RestoreFailed unresolved-declarations $pending-sources) $failure))))
               ($other $other)))))
        ($other $other)))))

(= (math:restore-row $project $row)
   (case $row
     (((Math:Attempt $id $goal $strategy $key $proof $verdict $raw)
        (let $records (math:records $project)
          (case (add-atom $records (Math:PriorAttempt $id $goal $strategy $key $proof $verdict $raw))
            (($done Math:RowRestored)))))
      ($other
        (if (== (math:research-row $row) ()) (Math:RestoreFailed malformed-record)
          (let $records (math:records $project)
            (case (add-atom $records $row) (($done Math:RowRestored)))))))))
(= (math:restore-rows $project $rows)
   (case $rows
     ((() Math:RowsRestored)
      ($some
        (let ($first $rest) (decons-atom $some)
          (case (math:restore-row $project $first)
            ((Math:RowRestored (math:restore-rows $project $rest))
             ($other $other))))))))
(= (math:restore-compared $project $research $history $expected)
   (let $actual (math:theory-rows $project)
     (if (math:same-rows $actual $expected)
       (case (math:restore-rows $project $research)
         ((Math:RowsRestored
            (let* (($records (math:records $project)) ($archive (math:encode-artifact $history)))
              (case (add-atom $records (Math:Archive $archive))
                (($done (Math:Restored $project))))))
          ($other $other)))
       (Math:RestoreFailed theory-mismatch))))

(= (math:quoted? $value)
   (case $value (((quote $body) True) ($other False))))
(= (math:checker? $kind) (or (== $kind type) (== $kind hol)))
(= (math:find-goal-row $rows $id)
   (case $rows
     ((() (Math:Missing goal $id))
      ($some
        (let ($first $rest) (decons-atom $some)
          (case $first
            (((Math:Goal $stored $kind $statement $description)
               (if (== $stored $id) $first (math:find-goal-row $rest $id)))
             ($other (math:find-goal-row $rest $id)))))))))
(= (math:has-goal? $rows $id)
   (case (math:find-goal-row $rows $id)
     (((Math:Goal $stored $kind $statement $description) True) ($other False))))
(= (math:research-key $row)
   (case $row
     (((Math:Goal $id $kind $statement $description) (goal $id))
      ((Math:Candidate $id $goal $strategy $key $proof) (candidate $id))
      ((Math:Attempt $id $goal $strategy $key $proof $verdict $raw) (attempt $id))
      ((Math:PriorAttempt $id $goal $strategy $key $proof $verdict $raw) (attempt $id))
      ($other none))))
(= (math:contains-key? $keys $key)
   (case $keys
     ((() False)
      ($some (let ($first $rest) (decons-atom $some)
               (if (=alpha $first $key) True (math:contains-key? $rest $key)))))))
(= (math:evidence-row? $id $goal $strategy $proof $all)
   (and (math:text-id? $id) (and (math:text-id? $strategy)
     (and (math:has-goal? $all $goal) (math:quoted? $proof)))))
(= (math:research-row? $row $all)
   (case $row
     (((Math:Goal $id $kind $statement $description)
        (and (math:text-id? $id) (and (math:checker? $kind)
          (and (math:quoted? $statement) (== (std:get-type $description) String)))))
      ((Math:Candidate $id $goal $strategy $key $proof)
        (math:evidence-row? $id $goal $strategy $proof $all))
      ((Math:Attempt $id $goal $strategy $key $proof $verdict $raw)
        (math:evidence-row? $id $goal $strategy $proof $all))
      ((Math:PriorAttempt $id $goal $strategy $key $proof $verdict $raw)
        (math:evidence-row? $id $goal $strategy $proof $all))
      ((Math:Note $id $text $sources) (math:text-id? $id))
      ((Math:Link $source $relation $target) True)
      ((Math:Archive (Math:Artifact $digest $text))
        (and (math:text-id? $digest) (== (std:get-type $text) String)))
      ($other False))))
(= (math:research-rows? $remaining $all $keys)
   (case $remaining
     ((() True)
      ($some
        (let* ((($row $rest) (decons-atom $some)) ($key (math:research-key $row)))
          (if (math:research-row? $row $all)
            (if (== $key none) (math:research-rows? $rest $all $keys)
              (if (math:contains-key? $keys $key) False
                (let $next (cons-atom $key $keys) (math:research-rows? $rest $all $next))))
            False))))))
(= (math:source? $source)
   (case $source
     ((Math:BaseProfile True)
      ((quote $call)
        (if (== (get-metatype $call) Expression)
          (let $head (car-atom $call) (math:declaration-head $head)) False))
      ($other False))))
(= (math:sources? $sources)
   (case $sources
     ((() True)
      ($some (let ($first $rest) (decons-atom $some)
               (if (math:source? $first) (math:sources? $rest) False))))))
(= (math:publications? $publications $research)
   (case $publications
     ((() True)
      ($some
        (let ($first $rest) (decons-atom $some)
          (case $first
            (((Math:Publication $name $goal $kind $statement $proof)
               (and (== (get-metatype $name) Symbol)
                 (and (math:quoted? $proof)
                   (case (math:find-goal-row $research $goal)
                     (((Math:Goal $stored $guest $claim $description)
                        (if (and (== $kind $guest) (=alpha $statement $claim))
                          (math:publications? $rest $research) False))
                      ($other False))))))
             ($other False))))))))
(= (math:contents? $name $sources $publications $research $history $expected $budget)
   (and (math:text-id? $name)
     (and (math:natural? $budget)
       (and (== (get-metatype $sources) Expression)
         (and (== (get-metatype $publications) Expression)
           (and (== (get-metatype $research) Expression)
             (and (== (get-metatype $history) Expression)
               (and (== (get-metatype $expected) Expression)
                 (and (math:sources? $sources)
                   (and (math:research-rows? $research $research ())
                        (math:publications? $publications $research)))))))))))
(= (math:restore-contents $name $sources $publications $research $history $expected $budget)
   (if (math:contents? $name $sources $publications $research $history $expected $budget)
    (let* (($project (math:new $name)) ($theory (math:theory $project)))
    (chain (remove-atom $theory (hol:profile base)) $initial-profile
     (case (math:replay-context $project $sources $publications $budget)
       ((Math:ContextReplayed
          (math:restore-compared $project $research $history $expected))
        ($other $other)))))
    (Math:RestoreFailed snapshot-records)))
(= (math:decode-artifact $artifact)
   (case $artifact
     (((Math:Artifact $digest $text)
        (if (and (== (std:get-type $digest) String) (== (std:get-type $text) String))
          (if (== $digest (str:sha256 $text))
            (let $value (parse $text) (Math:Decoded $value))
            (Math:RestoreFailed artifact-digest))
          (Math:RestoreFailed artifact-fields)))
      ($other (Math:RestoreFailed invalid-artifact)))))
(= (math:restore-bundle $bundle $budget)
   (case $bundle
     (((Math:Bundle 1 $name $source-artifact $publication-artifact $research-artifact $history-artifact $theory-artifact)
        (case ((math:decode-artifact $source-artifact) (math:decode-artifact $publication-artifact)
               (math:decode-artifact $research-artifact) (math:decode-artifact $history-artifact)
               (math:decode-artifact $theory-artifact))
          ((((Math:Decoded $sources) (Math:Decoded $publications) (Math:Decoded $research)
             (Math:Decoded $history) (Math:Decoded $expected))
             (math:restore-contents $name $sources $publications $research $history $expected $budget))
           ($other (Math:RestoreFailed component-decode)))))
      ($other (Math:RestoreFailed unsupported-bundle)))))
(= (math:restore-object $object $budget)
   (if (math:unique-json-object $object)
     (let* (($schema (json:get $object "schema")) ($payload (json:get $object "payload"))
            ($digest (json:get $object "sha256"))
            ($field-count (let (JsonObject $pairs) $object (size-atom $pairs))))
       (if (and (== $schema "prime.math-space.v1")
                (and (== $field-count 3)
                     (and (== (std:get-type $payload) String) (== (std:get-type $digest) String))))
         (if (== (str:sha256 $payload) $digest)
           (math:restore-bundle (parse $payload) $budget)
           (Math:RestoreFailed payload-digest))
         (Math:RestoreFailed snapshot-schema)))
     (Math:RestoreFailed duplicate-or-invalid-json)))
(= (math:restore $text $budget)
   (math:restore-object (json:parse $text) $budget))

(= (math:prior-attempt $project $id)
   (let $records (math:records $project)
     (new ($goal $strategy $key $proof $verdict $raw)
       (case (collapse (match $records
                  (Math:PriorAttempt $id $goal $strategy $key $proof $verdict $raw)
                  (Math:PriorAttempt $id $goal $strategy $key $proof $verdict $raw)){$id})
         ((($one) $one) ($other (Math:Missing prior-attempt $id)))))))
(= (math:resume-attempt $project $previous $next $budget)
   (case (math:prior-attempt $project $previous)
     (((Math:PriorAttempt $previous $goal $strategy $key $proof $verdict $raw)
        (math:attempt $project $next $goal $strategy $proof $budget))
      ($other $other))))
