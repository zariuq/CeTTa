; Stale dependencies.  A retained residual has read the space it searches.
; If that space changes before the residual resumes, the resumption is
; refused (`invalidated`) rather than continued against a world the residual
; did not see; a change to another space does not affect it.  A refused
; resumption is the currency check of the delimited-commitment core:
; resumption only in the world of the pause (Machines/ScopedCommit:
; resumeIfCurrent_determined, currency_check_refuses_changed_world), the
; residual counterpart of ResourceOwnership's
; RequestBorrow.View.transportIfCurrent_reset_rejected.  Within one computation a bound
; value stays the value that was read; a later read sees later writes.

(fact 1)
(fact 2)
(fact 3)
(= (facts) (match &self (fact $x) $x))

; 1. Unchanged space: the residual resumes and completes.
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (facts))
   (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
     (Pair $prefix (Pair $rest $t2))))

; 2. A write to another space: the residual resumes and completes.
!(bind! &other (new-space))
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (facts))
   (let $u (add-atom &other (fact 9))
     (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
       (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
       (Pair $prefix (Pair $rest $t2)))))

; 3. A write to the space the residual read: the resumption is refused.
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (facts))
   (let $u (add-atom &self (fact 4))
     (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
       (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
       (Pair $prefix (Pair $rest $t2)))))

; 4. A bound value is the value read; a read after the write sees the write.
(cfg 1)
!(let $v (match &self (cfg $x) $x)
   (let $u (remove-atom &self (cfg 1))
     (let $u2 (add-atom &self (cfg 2))
       (Pair $v (match &self (cfg $y) $y)))))
