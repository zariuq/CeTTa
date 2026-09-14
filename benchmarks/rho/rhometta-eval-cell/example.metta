; ===========================================================================
; rhometta-eval-cell -- the one idea, by itself: communication does computation.
;
; Two programs run side by side (rho:par).  One SENDS a message on a channel
; (a private "mailbox"); the other WAITS to receive on that same channel and
; then RUNS whatever arrives.  When the send and the receive meet -- a COMM --
; the message is delivered and run.  Here the message is a *deferred MeTTa term*
; (rhometta:eval ...), so the meeting EVALUATES it: one act of communication
; performs one computation step.
;
; And if the term has several results, the COMM forks -- one branch per result
; (this branching outcome set is what rhometta calls the "may-set").
;
; Run:  ./cetta --quiet --profile extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)

; Standard rho notation used below:
;   P | Q                 parallel composition
;   x!(m)                send process m on channel x
;   for (y <- x) { P }  receive from x, bind y in P
;   *y                   drop: run the quoted process/name y

; A channel is just a name; mint distinct mailboxes by number.
(= (chan $n) (if (== $n 0) (rho:quote rho:nil) (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; A cell: send a deferred term on mailbox 1, beside a receiver that runs it.
; Standard reading:
;   cell(job)  :=  1!(eval(job)) | for (y <- 1) { *y }
; So one COMM both delivers the payload and evaluates the deferred term.
(= (cell $job)
   (rho:par (rho:send (chan 1) (rhometta:eval $job))
            (rho:recv (chan 1) $y (rho:drop $y))))

; --- readable-output helpers (strip the rho:val wrapper; count branches) ------
(= (val-of (rho:val $x)) $x)
(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))

!("A. one COMM evaluates (+ 2 3), leaving the value:")
!(collapse (val-of (rhometta:run (cell (+ 2 3)))))

!("B. a payload with several results makes the COMM fork -- one branch each:")
!(collapse (val-of (rhometta:run (cell (superpose (a b c))))))

!("C. that fork is a real reduction step -- the one-step frontier count:")
!(count-tuple (collapse (rhometta:transitions (cell (superpose (a b c))))))
