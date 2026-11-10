; Sessions: the answer-set search one step at a time.
!(import! &self asp)
(= (run $s $n)
   (let $r (asp:step $s)
     (case $r
       (((running $events) (run $s (+ $n 1)))
        ((done $result) (finished $n $result))))))
(= (program)
   ((choice a () ()) (choice b () ()) (choice d () ())
    (rule c (a) ()) (constraint (a b) ()) (constraint () (a d))))
!(let $s (asp:start (program)) (run $s 0))
!(asp:solve (program))
; the state between steps is data
!(let $s (asp:start (program))
   (let $_ (asp:step $s)
     (case (asp:state $s)
       (((state $level $conflicts (trail $t) (true $ts) (false $fs)) (state-shape $level))
        ($other (unexpected $other))))))
; an unsatisfiable program finishes with the certificate asp:solve gives
(= (unsat-program) ((choice a () ()) (constraint (a) ()) (constraint () (a))))
!(let $s (asp:start (unsat-program)) (run $s 0))
!(asp:solve (unsat-program))
; closing ends the session
!(let $s (asp:start (program)) (let $c (asp:close $s) (c $c)))
