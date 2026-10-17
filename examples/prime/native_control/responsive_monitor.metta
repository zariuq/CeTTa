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
