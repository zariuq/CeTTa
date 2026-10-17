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
