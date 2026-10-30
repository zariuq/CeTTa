; Resumable candidate search over owned cooperative computations.
; Producers yield (IC:Candidate artifact) or (IC:ProducerFault reason).
; A take keeps every later answer in its work handle. Work and source epochs
; reject stale snapshots before another slice is executed.
; Materializers may return IC:Responses and IC:Continue events. Continuations
; carry inert proposal data; this controller alone creates their owned cursors.
!(import! &self (library work))
!(import! &self (library lib_ic_archive))
!(import! &self (library lib_fair_queue))

; Models own reconstruction and complete encoded acceptance. Search ordering
; neither supplies a proof nor turns an estimate into an acceptance cost.
(= (ic-search:observation IC:ProgramArchive $artifact)
   (case (ic-archive:prepare $artifact)
     (((IC:PreparedArchive $tree $catalog $source) (IC:Some ($catalog $source)))
      ($other IC:None))))
(= (ic-search:consider IC:ProgramArchive $original $best $candidate)
   (case (ic-archive:check $original $best $candidate)
     (((IC:ArchiveAccepted $next $bytes $before $after) (IC:CandidateAccepted $next))
      ((IC:ArchiveRejected $reason) (IC:CandidateRejected $reason)))))

; A proposal carries inert model data, not a callable. Materialization stays
; outside the retained generator, and cannot bypass complete-cost checking.
; Support counts source occurrences; it does not deduplicate equal packets.
(= (ic-search:materialize IC:ProgramArchive $original $proposal)
   (case $proposal
     (((IC:Factor $name $slots $patterns)
       (case (ic-archive:factor $original $name $slots $patterns)
         (((IC:Proposal $candidate $count)
           (if (>= $count 2) (IC:Candidate $candidate) (IC:Refused InsufficientSupport)))
          ($other (IC:ProducerFault InvalidGeneratedTemplate)))))
      ($other (IC:ProducerFault InvalidProposal)))))

; A named relational source for an explicit finite candidate region. Its
; recursive alternative remains available after the first answer.
(= (ic-search:from-list $items)
   (if (== $items ()) (superpose ()) (car-atom $items)))
(= (ic-search:from-list $items)
   (if (== $items ()) (superpose ()) (ic-search:from-list (cdr-atom $items))))

(= (ic-search:member? $value $values)
   (if (== $values ()) False
     (let ($head $tail) (decons-atom $values)
       (or (== $value $head) (ic-search:member? $value $tail)))))

(= (ic-search:fresh? $handle)
   (case (work-record $handle)
     (((route-record (route general) $strategy $law $modes $observer $footprint
         (work 0) $retained $owner (state more-possible) (taken 0) $transfers) True)
      ($other False))))

(= (ic-search:branches? $branches $seen)
   (if (== $branches ()) True
     (if (== (get-metatype $branches) Expression)
       (let ($branch $tail) (decons-atom $branches)
         (case $branch
           (((IC:Branch $cost $handle)
             (and (binary:unsigned? $cost)
               (and (not (ic-search:member? $handle $seen))
                 (and (ic-search:fresh? $handle)
                   (let $later (cons-atom $handle $seen) (ic-search:branches? $tail $later))))))
            ($other False)))) False)))

(= (ic-search:guards $sources)
   (if (== $sources ()) (IC:Some ())
     (if (== (get-metatype $sources) Expression)
       (let ($source $tail) (decons-atom $sources)
         (let $epoch (catch (work:source-epoch $source))
           (if (ic-search:source-epoch? $epoch)
             (case (ic-search:guards $tail)
               (((IC:Some $rest)
                 (let $guards (cons-atom (IC:Guard $source $epoch) $rest) (IC:Some $guards)))
                ($other IC:None))) IC:None))) IC:None)))
(= (ic-search:guards-current? $guards)
   (if (== $guards ()) True
     (let ($guard $tail) (decons-atom $guards)
       (case $guard
         (((IC:Guard $source $epoch)
           (case (catch (work:source-status $source $epoch))
             ((Work:Current (ic-search:guards-current? $tail)) ($not-current False))))
          ($other False))))))
(= (ic-search:source-epoch? $epoch)
   (case $epoch
     (((Work:SourceEpoch $identity $revision $dependencies)
       (and (== (binary:value-kind $identity) Text)
         (and (== (binary:value-kind $revision) Text) (== (binary:value-kind $dependencies) Text))))
      ($invalid-epoch False))))

(= (ic-search:open $branches $quantum $queue)
   (if (== $branches ()) (IC:Some $queue)
     (let ($branch $tail) (decons-atom $branches)
       (case $branch
         (((IC:Branch $cost $handle)
           (let $route (work-route $handle fifo)
             (if (== $route ())
               (let $configured (work-budget $handle $quantum)
                 (if (== $configured ())
                   (let* (($epoch (work:epoch $handle))
                          ($next (fair-queue:offer $queue $cost (IC:Cursor $handle $epoch))))
                     (case $next
                       (((Fair:Queue $phase $ticket $entries) (ic-search:open $tail $quantum $next))
                        ($other IC:None)))) IC:None)) IC:None))))))))

(= (ic:prepare $model $original $sources $quantum $branches)
   (if (and (binary:unsigned? $quantum) (> $quantum 0))
     (case (ic-search:observation $model $original)
       (((IC:Some $observation)
         (if (ic-search:branches? $branches ())
           (case (ic-search:guards $sources)
             (((IC:Some $guards)
               (case (ic-search:open $branches $quantum (fair-queue:empty))
                 (((IC:Some $queue)
                   (IC:PreparedSearch
                     (IC:SearchState (IC:Task $model $original $observation $guards $quantum)
                       $queue $original (IC:Statistics 0 0 0 0) IC:NoFault)))
                  ($other (IC:SearchRefused UnqualifiedProducer)))))
              ($other (IC:SearchRefused InvalidSources))))
           (IC:SearchRefused InvalidProducers)))
        ($other (IC:SearchRefused InvalidTask))))
     (IC:SearchRefused InvalidQuantum)))

(= (ic-search:cursors-current? $entries $seen)
   (if (== $entries ()) True
     (let ($entry $tail) (decons-atom $entries)
       (case $entry
         (((Fair:Entry $ticket $cost (IC:Cursor $handle $expected))
           (case (work:epoch $handle)
             (((Work:Fault $reason) False)
              ($actual (and (== $actual $expected)
                (and (not (ic-search:member? $handle $seen))
                  (let $later (cons-atom $handle $seen)
                    (ic-search:cursors-current? $tail $later))))))))
          ($other False))))))

(= (ic-search:statistics? $statistics)
   (case $statistics
     (((IC:Statistics $turns $candidates $accepted $faults)
       (and (binary:unsigned? $turns) (and (binary:unsigned? $candidates)
         (and (binary:unsigned? $accepted)
           (and (binary:unsigned? $faults) (<= $accepted $candidates))))))
      ($other False))))
(= (ic-search:room? $next $statistics)
   (case $statistics
     (((IC:Statistics $turns $candidates $accepted $faults)
       (and (binary:unsigned? (+ $next 1))
         (and (binary:unsigned? (+ $turns 1))
           (and (binary:unsigned? (+ $candidates 1))
             (and (binary:unsigned? (+ $accepted 1)) (binary:unsigned? (+ $faults 2))))))))))

(= (ic-search:result $state $reason)
   (case $state
     (((IC:SearchState $task $queue $best $statistics $first)
       (IC:SearchResult $best $statistics $reason $first $state)))))
(= (ic:cancel $state) (ic-search:result $state Cancelled))

(= (ic-search:with-fault $statistics $first $ticket $reason)
   (case $statistics
     (((IC:Statistics $turns $candidates $accepted $faults)
       (let $saved (case $first
                    ((IC:NoFault (IC:FirstFault $ticket $reason)) ($other $first)))
         (IC:Faulted (IC:Statistics $turns $candidates $accepted (+ $faults 1)) $saved))))))

(= (ic-search:continue $queue $quantum $cost $proposal)
   (if (binary:unsigned? $cost)
     (let $handle (work-new (ic-search:from-list ((IC:Proposal $proposal))))
       (case (ic-search:open ((IC:Branch $cost $handle)) $quantum $queue)
         (((IC:Some $next) (IC:Some $next))
          ($other (let $released (work-cancel $handle) (IC:ContinuationFault Capacity))))))
     (IC:ContinuationFault InvalidPriority)))

(= (ic-search:consume $model $original $best $statistics $first $ticket $prefix $queue $quantum)
   (if (== $prefix ()) (IC:Consumed $best $statistics $first $queue)
     (let ($answer $tail) (decons-atom $prefix)
       (case $answer
         (((IC:Proposal $proposal)
           (let $event (ic-search:materialize $model $original $proposal)
             (let $pending (cons-atom $event $tail)
               (ic-search:consume $model $original $best $statistics $first $ticket $pending $queue $quantum))))
          ((IC:Responses $events)
           (if (== (get-metatype $events) Expression)
             (let $pending (union-atom $events $tail)
               (ic-search:consume $model $original $best $statistics $first $ticket $pending $queue $quantum))
             (ic-search:consume $model $original $best $statistics $first $ticket
               (cons-atom (IC:ProducerFault InvalidResponses) $tail) $queue $quantum)))
          ((IC:Continue $cost $proposal)
           (case (ic-search:continue $queue $quantum $cost $proposal)
             (((IC:Some $next)
               (ic-search:consume $model $original $best $statistics $first $ticket $tail $next $quantum))
              ((IC:ContinuationFault $reason)
               (ic-search:consume $model $original $best $statistics $first $ticket
                 (cons-atom (IC:ProducerFault $reason) $tail) $queue $quantum)))))
          ((IC:Refused $reason)
           (case $statistics
             (((IC:Statistics $turns $candidates $accepted $faults)
               (ic-search:consume $model $original $best
                 (IC:Statistics $turns (+ $candidates 1) $accepted $faults) $first $ticket $tail $queue $quantum)))))
          ((IC:Candidate $candidate)
           (case $statistics
             (((IC:Statistics $turns $candidates $accepted $faults)
               (case (ic-search:consider $model $original $best $candidate)
                 (((IC:CandidateAccepted $next)
                   (ic-search:consume $model $original $next
                     (IC:Statistics $turns (+ $candidates 1) (+ $accepted 1) $faults) $first $ticket $tail $queue $quantum))
                  ((IC:CandidateRejected $reason)
                   (ic-search:consume $model $original $best
                     (IC:Statistics $turns (+ $candidates 1) $accepted $faults) $first $ticket $tail $queue $quantum))))))))
          ((IC:ProducerFault $reason)
           (case (ic-search:with-fault $statistics $first $ticket $reason)
             (((IC:Faulted $stats $saved) (ic-search:consume $model $original $best $stats $saved $ticket $tail $queue $quantum)))))
          ($other
           (case (ic-search:with-fault $statistics $first $ticket InvalidProducerObservation)
             (((IC:Faulted $stats $saved) (ic-search:consume $model $original $best $stats $saved $ticket $tail $queue $quantum))))))))))

(= (ic-search:finish-slice $task $remaining $best $statistics $first $ticket $cost $handle $status)
   (case $status
     ((complete
       (let $released (work-cancel $handle) (IC:Some (IC:SearchState $task $remaining $best $statistics $first))))
      (paused (ic-search:keep-slice $task $remaining $best $statistics $first $cost $handle))
      (more-possible (ic-search:keep-slice $task $remaining $best $statistics $first $cost $handle))
      ($other
       (case (ic-search:with-fault $statistics $first $ticket $status)
         (((IC:Faulted $stats $saved)
           (let $released (work-cancel $handle) (IC:Some (IC:SearchState $task $remaining $best $stats $saved))))))))))
(= (ic-search:keep-slice $task $remaining $best $statistics $first $cost $handle)
   (let* (($epoch (work:epoch $handle))
          ($queue (fair-queue:offer $remaining $cost (IC:Cursor $handle $epoch))))
     (case $queue
       (((Fair:Queue $phase $next $entries) (IC:Some (IC:SearchState $task $queue $best $statistics $first)))
        ($other IC:None)))))

(= (ic-search:step
      (IC:SearchState (IC:Task $model $original $observation $guards $quantum) $queue $best
        (IC:Statistics $turns $candidates $accepted $faults) $first))
   (case (fair-queue:take $queue)
     (((Fair:Taken (Fair:Entry $ticket $cost (IC:Cursor $handle $epoch)) $remaining)
       (case (work-take $handle 1)
         (((taken $prefix $status)
           (case (ic-search:consume $model $original $best
                   (IC:Statistics (+ $turns 1) $candidates $accepted $faults) $first $ticket $prefix $remaining $quantum)
             (((IC:Consumed $next-best $statistics $saved $next-queue)
               (ic-search:finish-slice (IC:Task $model $original $observation $guards $quantum)
                 $next-queue $next-best $statistics $saved $ticket $cost $handle $status)))))
          ($other
           (ic-search:finish-slice (IC:Task $model $original $observation $guards $quantum)
             $remaining $best (IC:Statistics (+ $turns 1) $candidates $accepted $faults)
             $first $ticket $cost $handle $other)))))
      ($other IC:None))))

; Revalidate the winner with the original domain checker. Equality of decoded
; observations would incorrectly exclude a numerical tolerance policy. A
; changed winner must still satisfy the original task and beat its full cost.
(= (ic-search:checked-best? $model $original $observation $best)
   (if (== (ic-search:observation $model $original) (IC:Some $observation))
     (if (== $best $original) True
       (case (ic-search:consider $model $original $original $best)
         (((IC:CandidateAccepted $checked) (== $checked $best)) ($other False)))) False))

(= (ic:resume $state $budget)
   (case $state
     (((IC:SearchState (IC:Task $model $original $observation $guards $quantum) $queue $best $statistics $first)
       (if (and (binary:unsigned? $budget)
                (and (binary:unsigned? $quantum) (> $quantum 0)))
         (if (and (fair-queue:valid? $queue) (ic-search:statistics? $statistics))
           (if (ic-search:guards-current? $guards)
             (if (ic-search:checked-best? $model $original $observation $best)
               (ic-search:run $state $budget) (IC:ResumeRejected InvalidArtifact))
             (IC:ResumeRejected ChangedDependencies))
           (IC:ResumeRejected InvalidState))
         (IC:ResumeRejected InvalidBudget)))
      ($other (IC:ResumeRejected InvalidState)))))

(= (ic-search:run $state $budget)
   (case $state
     (((IC:SearchState (IC:Task $model $original $observation $guards $quantum)
         (Fair:Queue $phase $next $entries) $best $statistics $first)
       (if (ic-search:guards-current? $guards)
         (if (ic-search:cursors-current? $entries ())
           (if (== $entries ())
             (case $statistics
               (((IC:Statistics $turns $candidates $accepted $faults)
                 (ic-search:result $state (if (== $faults 0) RegionExhausted FaultedRegion)))))
             (if (== $budget 0) (ic-search:result $state BudgetExhausted)
               (if (ic-search:room? $next $statistics)
                 (case (ic-search:step $state)
                   (((IC:Some $later) (ic-search:run $later (- $budget 1)))
                    ($other (IC:ResumeRejected InvalidTransition))))
                 (ic-search:result $state Capacity))))
           (IC:ResumeRejected CursorChanged))
         (IC:ResumeRejected ChangedDependencies))))))

(= (ic:release $state)
   (case $state
     (((IC:SearchState $task (Fair:Queue $phase $next $entries) $best $statistics $first)
       (ic-search:release-entries $entries)))))
(= (ic-search:release-entries $entries)
   (if (== $entries ()) ()
     (let ($entry $tail) (decons-atom $entries)
       (case $entry
         (((Fair:Entry $ticket $cost (IC:Cursor $handle $epoch))
           (let $released (work-cancel $handle) (ic-search:release-entries $tail))))))))
