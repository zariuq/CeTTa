; Mathematical projects: scoped checking and inspectable research records.
; Records are data. Publication always invokes the selected native checker.
(scope:profile lexical-fresh per-call reference)
!(import! &self str)
!(import! &self json)
!(import! &self system)

(= (math:name (Math:Space $name $theory $records)) $name)
(= (math:theory (Math:Space $name $theory $records)) $theory)
(= (math:records (Math:Space $name $theory $records)) $records)

(= (math:hash $value)
   (let $text (repr $value) (str:sha256 $text)))
(= (math:code $value)
   (cons-atom quote (cons-atom $value ())))
(= (math:singleton $value)
   (cons-atom $value ()))
(= (math:natural? $value)
   (if (== (std:get-type $value) Number)
     (and (>= $value 0) (== (% $value 1) 0)) False))
(= (math:text-id? $value)
   (if (== (std:get-type $value) String) (> (str:char-length $value) 0) False))

(= (math:new $name)
   (function
     (let* (($theory (new-space)) ($records (new-space)) ($session (system:uuid4-hex)))
       (chain (add-atom $theory (hol:profile base)) $profile
         (chain (add-atom $records (Math:Session $session)) $identified
           (chain (add-atom $records (Math:Next 0)) $ready
             (return (Math:Space $name $theory $records))))))))

; One controller sequences a project's research events. Storage adapters
; provide the corresponding publication boundary when a project is saved.
(= (math:record-at $records $n $event)
   (case (remove-atom $records (Math:Next $n))
     (($removed
        (case (add-atom $records (Math:Event $n $event))
          (($written
             (let $number (+ $n 1)
               (case (add-atom $records (Math:Next $number))
                 (($next (Math:Recorded $n))))))))))))
(= (math:record-ready $project $event)
   (let* (($records (math:records $project))
          ($next (new ($n) (collapse (match $records (Math:Next $n) $n)))))
     (case $next
       ((($n) (math:record-at $records $n $event))
        ($bad (Math:Fault event-sequence $bad))))))
(= (math:record $project $event)
   (chain $event $ready (math:record-ready $project $ready)){$ready})

(= (math:history-range $records $index $end)
   (if (>= $index $end) ()
     (let $entries (new ($event)
                     (collapse (match $records (Math:Event $index $event) $event){$index}))
       (case $entries
         ((($event)
            (let* (($next (+ $index 1)) ($rest (math:history-range $records $next $end)))
              (cons-atom ($index $event) $rest)))
          ($bad (Math:Fault history-gap $index)))))))
(= (math:history $project)
   (let* (($records (math:records $project))
          ($next (new ($n) (collapse (match $records (Math:Next $n) $n)))))
     (case $next
       ((($n) (math:history-range $records 0 $n))
        ($bad (Math:Fault event-sequence $bad))))))

; A statement is quoted explicitly so inspection never executes its code.
(= (math:goal-save $project $id $expected)
   (function
     (let $records (math:records $project)
       (new ($k $s $d)
         (case (collapse (match $records (Math:Goal $id $k $s $d)
                           (Math:Goal $id $k $s $d)){$id})
         ((() (case (add-atom $records $expected)
                (($done (return (Math:GoalAdded $id))))))
          (($found)
             (if (=alpha $found $expected)
               (return (Math:GoalExists $id))
               (return (Math:Conflict goal $id))))
          ($other (return (Math:Conflict goal $id)))))))))
(= (math:goal $project $id $kind $statement $description)
   (case $statement
     (((quote $body)
        (let $expected (Math:Goal $id $kind $statement $description)
          (math:goal-save $project $id $expected)))
      ($other (Math:Refused unquoted-statement $id)))))

(= (math:goals $project)
   (let $records (math:records $project)
     (new ($id $kind $statement $description)
       (collapse (match $records (Math:Goal $id $kind $statement $description)
                    (Math:Goal $id $kind $statement $description))))))
(= (math:goal-entry $project $id)
   (let $records (math:records $project)
     (case (collapse (match $records (Math:Goal $id $kind $statement $description)
                           (Math:Goal $id $kind $statement $description)){$id})
       ((($one) $one) ($other (Math:Missing goal $id))))))

; Portable declaration inspection omits the native checker's private ledger.
(= (math:theory-row $row)
   (case $row
     (((hol:known $name $statement theorem $proof $deps $revision $signature) ())
      ((hol:known $name $statement typing $proof $deps $revision $signature) ())
      ((hol:known $name $statement $origin $proof $deps $revision $signature)
        ((Math:Declaration $name $statement $origin)))
      ($other ($other)))))
(= (math:theory-rows $project)
   (let* (($theory (math:theory $project)) ($rows (collapse (get-atoms $theory))))
     (foldl-atom $rows () $all $row
       (union-atom $all (math:theory-row $row)))))
(= (math:theory-digest $project)
   (let* (($rows (math:theory-rows $project)) ($ordered (sort-atom $rows))
          ($text (repr $ordered)))
     (str:sha256 $text)))
; Live task keys use the existing mutation counter, rather than printing the
; entire theory on every call. Every theory mutation conservatively expires
; pending evidence, including publication and remove-then-reinsert changes.
; Restored scopes receive a fresh session; old attempts remain historical.
(= (math:theory-key $project)
   (let* (($theory (math:theory $project)) ($records (math:records $project))
          ($revision (_minimal-space-revision $theory)))
     (new ($session)
       (case (collapse (match $records (Math:Session $session) $session))
         ((($session) (Math:ScopeKey $session $revision))
          ($other (Math:Fault ambiguous-session)))))))
(= (math:task-key $project $goal)
   (case (math:goal-entry $project $goal)
     (((Math:Goal $goal $kind $statement $description)
        (let* (($theory (math:theory-key $project))
               ($claim (Math:Claim $kind $statement)) ($claim-key (math:hash $claim)))
          (Math:TaskKey $theory $claim-key)))
      ($other $other))))

(= (math:check-native $project $kind (quote $statement) (quote $proof) $budget)
   (let $theory (math:theory $project)
     (if (== $budget 0) (Incomplete (checking-budget 0))
       (case $kind
       ((type (try (type:check $theory $proof $statement $budget)))
        (hol (try (hol:proves $theory $proof $statement $budget)))
        ($other (Math:UnsupportedChecker $kind)))))))
(= (math:verdict $raw)
   (case $raw
     ((True checked) (False rejected)
      ((Established $evidence) checked)
      ((Refuted $reason) (rejected $reason))
      ((Undetermined $reason) (unfinished $reason))
      ((Error $call $reason) (unfinished $reason))
      ((Incomplete $reason) (unfinished $reason))
      ($other (fault $other)))))

; Attempt IDs make repeated tool calls inspectable. They never authorize a
; publication: the publisher rechecks the actual statement and proof.
(= (math:attempt $project $id $goal $strategy (quote $proof) $budget)
   (function
     (case (math:goal-entry $project $goal)
       (((Math:Goal $goal $kind $statement $description)
          (let* (($key (math:task-key $project $goal))
                 ($raw (math:check-native $project $kind $statement (quote $proof) $budget))
                 ($verdict (math:verdict $raw))
                 ($record (Math:Attempt $id $goal $strategy $key (quote $proof) $verdict $raw))
                 ($records (math:records $project)))
            (case (collapse (match $records (Math:Attempt $id $g $s $k $p $v $r)
                                   (Math:Attempt $id $g $s $k $p $v $r)){$id})
              ((() (case (add-atom $records $record)
                     (($done (return $record)))))
               (($previous)
                  (if (=alpha $previous $record) (return $record)
                    (return (Math:Conflict attempt $id))))
               ($other (return (Math:Conflict attempt $id)))))))
        ($other (return $other))))))

(= (math:attempt-entry $project $id)
   (let $records (math:records $project)
     (case (collapse (match $records
                         (Math:Attempt $id $goal $strategy $key $proof $verdict $raw)
                         (Math:Attempt $id $goal $strategy $key $proof $verdict $raw)){$id})
       ((($one) $one) ($other (Math:Missing attempt $id))))))
(= (math:attempt-status $project $id)
   (case (math:attempt-entry $project $id)
     (((Math:Attempt $id $goal $strategy $key $proof $verdict $raw) $verdict)
      ($other $other))))

(= (math:candidate-entry $project $id)
   (let $records (math:records $project)
     (new ($goal $strategy $key $proof)
       (case (collapse (match $records (Math:Candidate $id $goal $strategy $key $proof)
                             (Math:Candidate $id $goal $strategy $key $proof)){$id})
         ((($one) $one) ($other (Math:Missing candidate $id)))))))
(= (math:propose $project $id $goal $strategy $proof)
   (case $proof
     (((quote $body)
        (case (math:goal-entry $project $goal)
          (((Math:Goal $goal $kind $statement $description)
             (case (math:candidate-entry $project $id)
               (((Math:Missing candidate $id)
                  (let* (($key (math:task-key $project $goal))
                         ($candidate (Math:Candidate $id $goal $strategy $key $proof))
                         ($records (math:records $project)))
                    (case (add-atom $records $candidate)
                      (($done (Math:Proposed $id $goal))))))
                ($existing (Math:Conflict candidate $id)))))
           ($other $other))))
      ($other (Math:Refused unquoted-proof $id)))))
(= (math:check $project $id $budget)
   (case (math:candidate-entry $project $id)
     (((Math:Candidate $id $goal $strategy $key $proof)
        (if (== $key (math:task-key $project $goal))
          (math:attempt $project $id $goal $strategy $proof $budget)
          (Math:Stale $id task-changed)))
      ($other $other))))
(= (math:attempts $project $goal)
   (let $records (math:records $project)
     (new ($id $strategy $key $proof $verdict $raw)
       (collapse (match $records
         (Math:Attempt $id $goal $strategy $key $proof $verdict $raw)
         (Math:Attempt $id $goal $strategy $key $proof $verdict $raw)){$goal}))))

(= (math:recheck $project $id $budget)
   (case (math:attempt-entry $project $id)
     (((Math:Attempt $id $goal $strategy $key $proof $verdict $raw)
        (if (== $key (math:task-key $project $goal))
          (case (math:goal-entry $project $goal)
            (((Math:Goal $goal $kind $statement $description)
               (Math:Rechecked $id
                 (math:verdict (math:check-native $project $kind $statement $proof $budget))))
             ($other $other)))
          (Math:Stale $id task-changed)))
      ($other $other))))

; Notes and links describe an investigation. A note does not establish a
; theorem, regardless of the words its author uses.
(= (math:note $project $id $text $sources)
   (function
     (let $records (math:records $project)
       (case (add-atom $records (Math:Note $id $text $sources))
         (($done (return (Math:Noted $id))))))))
(= (math:notes $project)
   (let $records (math:records $project)
     (new ($id $text $sources)
       (collapse (match $records (Math:Note $id $text $sources)
                    (Math:Note $id $text $sources))))))
(= (math:link $project $source $relation $target)
   (function
     (let $records (math:records $project)
       (case (add-atom $records (Math:Link $source $relation $target))
         (($done (return (Math:Linked $source $relation $target))))))))

; A selected declaration is read as syntax. It remains in its theory space.
(= (math:inspect $project $name)
   (let $theory (math:theory $project)
     (collapse (match $theory
     (hol:known $name $statement $origin $proof $deps $revision $signature)
     (Math:Declaration $name $statement $origin $deps)){$name})))

; Only native declaration operations are executable through this interface.
; In particular a proof strategy cannot add a helper axiom here.
(= (math:declaration-head $head)
   (case $head ((hol:inductive True) (hol:define True) ($other False))))
(= (math:native-name-result $name $result)
   (case $result
     (((Established ($actual))
        (if (and (== (get-metatype $name) Symbol) (== (get-metatype $actual) Symbol))
          (if (=alpha $actual $name) $result (Math:ActionNotEstablished $name (reported-name $actual)))
          (Math:ActionNotEstablished $name)))
      ($other $other))))
(= (math:declare $project (quote $declaration))
   (function
     (let* ((($head $arguments) (decons-atom $declaration))
            ($theory (math:theory $project)))
       (if (math:declaration-head $head)
         (let* (($call (cons-atom $head (cons-atom $theory $arguments)))
                ($name (car-atom $arguments))
                ($result (try (eval $call))))
           (case (math:native-name-result $name $result)
             (((Established $evidence)
                (case (math:record $project (Math:DeclarationSource (quote $declaration)))
                  (($recorded (return (Established $evidence))))))
              ($result (return $result)))))
         (return (Math:Refused declaration-head $head))))))

(= (math:publish-native $project $name $kind (quote $statement) (quote $proof) $budget)
   (let* (($theory (math:theory $project))
          ($checked (math:check-native $project $kind (quote $statement) (quote $proof) $budget)))
     (case (math:verdict $checked)
       ((checked
          (case $kind
            ((type
               (let $result (type:theorem $theory $name $statement $proof)
                 (math:published-name-result $name $result)))
             (hol
               (let $result (hol:theorem $theory $name $statement $proof)
                 (math:published-name-result $name $result)))
             ($other (Math:UnsupportedChecker $kind)))))
        ($other $checked)))))

; Inspecting a HOL publication with try does not perform its admission.
; Publication above executes the native action and verifies its returned name.
(= (math:published-name-result $name $result)
   (if (and (== (get-metatype $name) Symbol) (== (get-metatype $result) Symbol))
     (if (=alpha $name $result)
       (let $evidence (math:singleton $result) (Established $evidence))
       (Math:ActionNotEstablished $name))
     (Math:ActionNotEstablished $name)))

(= (math:publish-result $project $attempt $name $goal $kind $statement $proof $result)
   (case $result
     (((Established $evidence)
        (case (math:record $project (Math:Publication $name $goal $kind $statement $proof))
          (($recorded (Math:Published $name $attempt)))))
      ($other (Math:PublicationFailed $name $other)))))
(= (math:publish-at $project $attempt $goal $key $proof $name $budget)
   (if (== $key (math:task-key $project $goal))
     (case (math:goal-entry $project $goal)
       (((Math:Goal $goal $kind $statement $description)
          (let $result (math:publish-native $project $name $kind $statement $proof $budget)
            (math:publish-result $project $attempt $name $goal $kind $statement $proof $result)))
        ($other $other)))
     (Math:Stale $attempt task-changed)))
(= (math:publish $project $attempt $name $budget)
   (case (math:attempt-entry $project $attempt)
     (((Math:Attempt $attempt $goal $strategy $key $proof $verdict $raw)
        (math:publish-at $project $attempt $goal $key $proof $name $budget))
      ($other $other))))

(= (math:run $project (quote $expression))
   (let $theory (math:theory $project)
     (let $result (eval $expression (admitted $theory))
       (case $result
         (((eval $residual (admitted $scope))
            (let $code (math:code $residual) (Math:Residual $code)))
          ($other $other))))))

; A restored attempt is tried under the current task with a new attempt ID.
; Its previous result remains an observation of the previous run.
(= (math:retry $project $previous $next $budget)
   (case (math:attempt-entry $project $previous)
     (((Math:Attempt $previous $goal $strategy $key $proof $verdict $raw)
        (math:attempt $project $next $goal $strategy $proof $budget))
      ($other $other))))

(= (math:publication-check $project $goal $name $kind $statement $budget)
   (let $proof
     (if (== $kind hol)
       (let $citation (cons-atom pf:known (math:singleton $name)) (math:code $citation))
       (math:code $name))
     (let $raw (math:check-native $project $kind $statement $proof $budget)
       (case (math:verdict $raw)
         ((checked (Math:GoalStatus $goal checked (published $name)))
          ($other (Math:GoalStatus $goal open (publication $name $other))))))))
(= (math:find-publication $project $goal $names $kind $statement $budget)
   (case $names
     ((() (Math:GoalStatus $goal open no-current-publication))
      ($some
        (let* (($name (car-atom $some)) ($rest (cdr-atom $some))
               ($result (math:publication-check $project $goal $name $kind $statement $budget)))
          (case $result
            (((Math:GoalStatus $goal checked $evidence) $result)
             ($other (math:find-publication $project $goal $rest $kind $statement $budget)))))))))
(= (math:goal-status-in $project $goal $kind $statement $budget)
   (let $records (math:records $project)
     (new ($n $name $stored-kind $stored-statement $proof)
       (let $publications (collapse (match $records
                 (Math:Event $n (Math:Publication $name $goal $stored-kind $stored-statement $proof))
                 $name){$goal})
         (math:find-publication $project $goal $publications $kind $statement $budget)))))
(= (math:goal-status $project $goal $budget)
   (case (math:goal-entry $project $goal)
     (((Math:Goal $goal $kind $statement $description)
        (math:goal-status-in $project $goal $kind $statement $budget))
      ($other $other))))
