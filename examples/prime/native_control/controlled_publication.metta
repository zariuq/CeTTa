; Pause before publishing Alpha's prepared transaction. Beta can still use
; the shared permit; resuming Alpha must validate its retained proposal.
; Recording is not requested. The breakpoint matches the source directive.
!(import! &self ../../../lib/native_history.metta)
!(bind! &jobs (new-space))
!(add-atom &jobs Permit)
!(add-atom &jobs (exec 10 (I (take Permit)) (O (+ (Winner Alpha)))))
!(add-atom &jobs (exec 0 (I (take Permit)) (O (+ (Winner Beta)))))

!(let $report
   (select 1 (search-policy (control FIFO (resources Transaction)
      (retain True) (budget 10000) (breakpoint (exec 10 $inputs $outputs)))) &jobs)
   (chain (bind! &paused $report) $ignored (history:field $report status)))
!(collapse (match &jobs Permit PermitPresent))

!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $agenda))
   (select 1 (search-policy (control WO (resources Transaction) (budget 10000)
      (key (exec $priority $inputs $outputs) $priority))) &jobs)
   (AlternativeCommitted $s (size-atom $values)))

!(bind! &held (let $report &paused
   (let (HistoryField Found residual $quoted) (history:field $report residual)
      (let $tail (cdr-atom $quoted) (car-atom $tail)))))
!(let $report
   (select 1 (search-policy (control FIFO (resources Transaction)
      (budget 10000) (breakpoint False))) &held)
   (RetainedProposal (history:field $report status)
      (history:field $report values)))
!(collapse (match &jobs (Winner $who) $who))

; A strategy can also inspect eligible interactions and choose Beta directly.
; Discovery and a zero-budget take publish nothing. The returned search owner
; resumes through the same transaction controller, without recording history.
; Once selected, it no longer needs the catalogue of unchosen preparations.
!(bind! &chosen-jobs (new-space))
!(add-atom &chosen-jobs Permit)
!(add-atom &chosen-jobs (exec Alpha (I (take Permit)) (O (+ (Winner Alpha)))))
!(add-atom &chosen-jobs (exec Beta (I (take Permit)) (O (+ (Winner Beta)))))
!(let (InteractionChoices (choices $choices) (status $status) (pending $pending)
       (quanta $quanta) (residual $residual))
   (interaction:choices &chosen-jobs 1000)
   (chain (bind! &ticket-owner $residual) $ignored
      (Available (size-atom $choices) $status)))
!(bind! &selected (interaction:perform &ticket-owner 2 0))
!(collapse (match &chosen-jobs Permit StillPresent))
!(bind! &controlled-choice
   (let (Observation $values (satisfied $s) (closed $c) (residual $r)
         (work $w) (status $t) (agenda $agenda)) &selected $r))
!(interaction:dispose &ticket-owner)
!(let $report (interaction:perform &controlled-choice 1000)
   (Selected (history:field $report status) (history:field $report values)))
!(collapse (match &chosen-jobs (Winner $who) $who))

; Raw MM2 removes its directive before matching, so inspection stops earlier.
; The stopped directive remains present until this same owner resumes.
!(bind! &raw-jobs (new-space))
!(add-atom &raw-jobs Permit)
!(add-atom &raw-jobs (exec 0 (, Permit) (O (+ (Winner Raw)) (- Permit))))
!(let $report
   (select 1 (search-policy (control FIFO (resources MM2) (budget 10000)
      (breakpoint (exec 0 $inputs $outputs)))) &raw-jobs)
   (chain (bind! &raw-paused $report) $ignored
      (RawBreakpoint (history:field $report status))))
!(collapse (match &raw-jobs (exec $key $inputs $outputs) DirectivePresent))
!(bind! &raw-held (let $report &raw-paused
   (let (HistoryField Found residual $quoted) (history:field $report residual)
      (let $tail (cdr-atom $quoted) (car-atom $tail)))))
!(let $report
   (select 1 (search-policy (control FIFO (resources MM2) (budget 10000)
      (breakpoint False))) &raw-held)
   (RawCompleted (history:field $report status)))
!(collapse (match &raw-jobs (Winner $who) $who))

; Independent matchers use two workers; publication stays on the coordinator.
; Stopping one private world retains the other history and the live source.
!(bind! &snapshot-jobs (new-space))
!(add-atom &snapshot-jobs Token)
!(add-atom &snapshot-jobs (exec Left (I (read Token)) (O (+ Left))))
!(add-atom &snapshot-jobs (exec Right (I (read Token)) (O (+ Right))))
!(let $report
   (select 100 (search-policy (control FIFO (resources (Transaction worlds))
      (workers 2) (budget 10000) (breakpoint (exec $key $inputs $outputs)))) &snapshot-jobs)
   (chain (bind! &snapshot-paused $report) $ignored
      (SnapshotBreakpoint (history:field $report status))))
!(collapse (match &snapshot-jobs Token TokenPresent))

!(bind! &snapshot-held (let $report &snapshot-paused
   (let (HistoryField Found residual $quoted) (history:field $report residual)
      (let $tail (cdr-atom $quoted) (car-atom $tail)))))
!(let (Observation $values (satisfied $s) (closed $closed) (residual $residual)
      (work $work) (status $status) (agenda $agenda))
   (select 100 (search-policy (control FIFO (resources (Transaction worlds))
      (workers 2) (budget 10000) (breakpoint False))) &snapshot-held)
   (PrivateWorlds (size-atom $values) $closed))
!(collapse (match &snapshot-jobs Token TokenPresent))

; Choose an actual raw activation before it removes its directive or matches.
; The explicit choice changes the default byte order. Its residual returns to
; the ordinary MM2 provider, whose remaining rule finds the permit consumed.
!(bind! &raw-choice-jobs (new-space))
!(add-atom &raw-choice-jobs Permit)
!(add-atom &raw-choice-jobs (exec 0 (, Permit) (O (+ (Winner Default)) (- Permit))))
!(add-atom &raw-choice-jobs (exec 9 (, Permit) (O (+ (Winner Chosen)) (- Permit))))
!(let (InteractionChoices (choices $choices) (status $status) (pending $pending)
       (quanta $quanta) (residual $residual))
   (interaction:choices &raw-choice-jobs MM2 1000)
   (chain (bind! &raw-choice-owner $residual) $ignored
      (RawAvailable (size-atom $choices) $status)))
!(bind! &raw-chosen (interaction:perform &raw-choice-owner 2 0))
!(collapse (match &raw-choice-jobs Permit StillPresent))
!(bind! &raw-choice-held
   (let (Observation $values (satisfied $s) (closed $c) (residual $r)
         (work $w) (status $t) (agenda $agenda)) &raw-chosen $r))
!(interaction:dispose &raw-choice-owner)
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $agenda))
   (interaction:perform &raw-choice-held 1000)
   (RawSelected $s (size-atom $values)))
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $agenda))
   (select 7 (search-policy (control FIFO (resources MM2) (budget 1000))) &raw-choice-held)
   (RawRemainder $c (size-atom $values)))
!(collapse (match &raw-choice-jobs (Winner $who) $who))

; Choose a binding within one directive. The first-binding Transaction
; profile remains available; matches exposes each complete input tuple.
!(bind! &binding-jobs (new-space))
!(add-atom &binding-jobs (Fact Alpha))
!(add-atom &binding-jobs (Fact Beta))
!(add-atom &binding-jobs (exec Pick (I (take (Fact $who))) (O (+ (Selected $who)))))
!(let (InteractionChoices (choices $choices) (status $status) (pending $pending)
       (quanta $quanta) (residual $residual))
   (interaction:choices &binding-jobs (Transaction matches) 1000)
   (chain (bind! &binding-owner $residual) $ignored
      (AvailableBindings (size-atom $choices) $status)))
!(bind! &binding-choice (interaction:perform &binding-owner 2 0))
!(bind! &binding-held
   (let (Observation $values (satisfied $s) (closed $c) (residual $r)
         (work $w) (status $t) (agenda $agenda)) &binding-choice $r))
!(interaction:dispose &binding-owner)
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $agenda))
   (interaction:perform &binding-held 1000)
   (BindingSelected $s (size-atom $values)))
!(collapse (match &binding-jobs (Selected $who) $who))

; A query finishes its captured bag after its own writes. A later query
; sees the reached world, including new matching rows and duplicates.
!(bind! &captured-facts (new-space))
!(add-atom &captured-facts (Fact A))
!(add-atom &captured-facts (Fact A))
!(add-atom &captured-facts (Fact B))
(= (publish-from-captured)
   (match &captured-facts (Fact $who)
      (chain (add-atom &captured-facts (Fact New)) $ignored $who)))
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t))
   (select 100 (search-policy (control FIFO (workers 2) (quantum 3) (budget 10000)))
      (publish-from-captured))
   (CapturedThenFresh $values (collapse (match &captured-facts (Fact $who) $who))))

; The worlds profile chooses the same physical bindings, but publishes
; into a private world. The original parent remains unchanged and current.
!(bind! &private-jobs (new-space))
!(add-atom &private-jobs (Fact Alpha))
!(add-atom &private-jobs (Fact Beta))
!(add-atom &private-jobs (exec Pick (I (take (Fact $who))) (O (+ (Selected $who)))))
!(let (InteractionChoices (choices $choices) (status $status) (pending $pending)
       (quanta $quanta) (residual $residual))
   (interaction:choices &private-jobs (Transaction worlds) 1000)
   (chain (bind! &private-owner $residual) $ignored
      (PrivateBindings (size-atom $choices) $status)))
!(bind! &private-choice (interaction:perform &private-owner 2 0))
!(bind! &private-held
   (let (Observation $values (satisfied $s) (closed $c) (residual $r)
         (work $w) (status $t) (agenda $agenda)) &private-choice $r))
!(interaction:dispose &private-owner)
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $agenda))
   (interaction:perform &private-held 1000)
   (SelectedPrivateWorld (size-atom $values) $c))
!(collapse (match &private-jobs (Fact $who) $who))

; An authored strategy selects the rightmost inspected Resume until an answer
; arrives. Earlier work stays owned; rightmost selection can starve it.
; Inspection computes no successor. Each grant services one native quantum,
; whose operation retains its own yielding contract.
(= (control:rightmost-resume $rows)
   (let $rest (cdr-atom $rows)
      (if (== $rest ())
          (let $row (car-atom $rows)
             (let $fields (cdr-atom $row) (car-atom $fields)))
          (control:rightmost-resume $rest))))
(= (control:one-answer $held $fuel)
   (if (== $fuel 0) Incomplete
      (let (InteractionChoices (choices $choices) (status $status) (pending $pending)
            (quanta $quanta) (residual $cursor))
         (interaction:choices $held Resume 1000)
         (if (== $choices ())
             (chain (interaction:dispose $cursor) $ignored Exhausted)
             (let $report (interaction:perform $cursor (control:rightmost-resume $choices) 1)
                (chain (interaction:dispose $cursor) $ignored
                   (let (Observation $values (satisfied $s) (closed $c) (residual $r)
                         (work $w) (status $t)) $report
                      (if (== $values ()) (control:one-answer $held (- $fuel 1))
                          (ControlledFirst $values)))))))))
!(bind! &resume-started
   (select 100 (search-policy (control FIFO (budget 0))) (superpose (A B B))))
!(bind! &resume-held
   (let (Observation $v (satisfied $s) (closed $c) (residual $r)
         (work $w) (status $t)) &resume-started $r))
!(control:one-answer &resume-held 100)
!(let (Observation $v (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t))
   (select 100 (search-policy (control FIFO (budget 10000))) &resume-held)
   (ControlledRemainder $v $c))
