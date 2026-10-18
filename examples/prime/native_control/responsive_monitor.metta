; A continuing agent yields through the existing owned execution budget.
; No history is requested. Empty answers do not mean the loop has completed.
; The counter distinguishes actual resumption from restarting the loop.
; The budget counts evaluator quanta, not elapsed time or all primitive work.
(= (monitor:loop Heartbeat $n)
   (chain (println! (Heartbeat $n)) $ignored (monitor:loop Service $n)))
(= (monitor:loop Service $n)
   (chain (println! (Service $n)) $ignored (monitor:loop Heartbeat (+ $n 1))))
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t))
   (select 0 (search-policy (control FIFO (demand all) (budget 20) (retain True)))
      (monitor:loop Heartbeat 0))
   (chain (println! (ControlReturned $w $c $t)) $ignored
      (let (Observation $more (satisfied $s2) (closed $c2) (residual $r2)
             (work $w2) (status $t2))
         (select 0 (search-policy (control FIFO (demand all) (budget 20) (retain True))) $r)
         (MonitorPaused $values $more $w $w2 $c $c2 $t $t2))))

; A request/reply server keeps its actual waiting computation between bursts.
; Each exec reads its own source body to restore the receive after a commit.
; Slot bounds the reply buffer; Acknowledge authorizes its consumer to release
; that capacity. Equal request payloads remain separate bag occurrences.
!(bind! &server-jobs (new-space))
!(add-atom &server-jobs Slot)
!(add-atom &server-jobs
   (exec Server
      (I (take Slot) (take (Request $id $payload))
         (read (exec Server $input $output)))
      (O (+ (Reply $id $payload)) (+ (exec Server $input $output)))))
!(add-atom &server-jobs
   (exec Consumer
      (I (take (Reply $id $payload)) (take Acknowledge)
         (read (exec Consumer $input $output)))
      (O (+ Slot) (+ (Delivered $id $payload))
         (+ (exec Consumer $input $output)))))
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $a))
   (select 100 (search-policy (control FIFO (resources Transaction)
      (demand additional) (budget 10000))) &server-jobs)
   (chain (bind! &server-held $r) $ignored
      (ServerWaiting $t $c (size-atom $values))))

!(add-atom &server-jobs (Request Earlier Same))
!(add-atom &server-jobs (Request Earlier Same))
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $a))
   (select 100 (search-policy (control FIFO (resources Transaction)
      (demand additional) (budget 10000))) &server-held)
   (ReplyBuffered $t $c (size-atom $values)))
!(collapse (match &server-jobs (Request $id $payload) (Request $id $payload)))
!(collapse (match &server-jobs (Reply $id $payload) (Reply $id $payload)))
!(collapse (match &server-jobs Slot CapacityAvailable))

!(add-atom &server-jobs Acknowledge)
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $a))
   (select 100 (search-policy (control FIFO (resources Transaction)
      (demand additional) (budget 10000))) &server-held)
   (FirstAcknowledged $t (size-atom $values)))
!(collapse (match &server-jobs (Delivered $id $payload) (Delivered $id $payload)))
!(collapse (match &server-jobs (Reply $id $payload) (Reply $id $payload)))

!(add-atom &server-jobs Acknowledge)
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $a))
   (select 100 (search-policy (control FIFO (resources Transaction)
      (demand additional) (budget 10000))) &server-held)
   (BurstDrained $t (size-atom $values)))
!(collapse (match &server-jobs (Delivered $id $payload) (Delivered $id $payload)))

!(add-atom &server-jobs (Request Later Same))
!(add-atom &server-jobs Acknowledge)
!(let (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (agenda $a))
   (select 100 (search-policy (control FIFO (resources Transaction)
      (demand additional) (budget 10000))) &server-held)
   (LaterBurst $t $c (size-atom $values)))
!(collapse (match &server-jobs (Delivered $id $payload) (Delivered $id $payload)))
!(collapse (match &server-jobs Slot CapacityAvailable))
