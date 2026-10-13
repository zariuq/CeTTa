; Changing what is observed is not discarding retained work.
;
; `once` observes one answer and discards the rest of the search: the
; alternatives after its answer never run (Machines/ScopedCommit:
; run_once_eq_select, once_stops_before_losing_effect).  A bounded selection
; that retains its residual observes the same prefix, and resuming the
; residual where the pause left the world gives the rest of the uninterrupted
; run: every answer once, duplicates included, nothing repeated
; (select_then_resume; VisiblePrefixAdapter: run_add,
; paused_state_retains_allowance_and_suffix).  A demand of zero answers
; inspects nothing (zero_demand_no_inspection).

(= (finite) 1)
(= (finite) 2)
(= (finite) 2)
(= (finite) 3)
(= (nats $n) $n)
(= (nats $n) (nats (+ $n 1)))

; 1. The whole search, for reference.
!(collapse (finite))

; 2. `once` and a one-answer selection agree on the answer.
!(once (finite))
!(select 1 (finite))

; 3. Two answers retained with their residual; resuming gives the other two.
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 2 (search-policy (control FIFO (retain True))) (finite))
   (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
     (Pair $prefix (Pair $rest $t2))))

; 4. An infinite search: three answers, then two additional ones from the
;    residual, are the first five answers.  A demand counts answers from the
;    start of the search unless it says `additional`: an absolute demand of
;    two is already met by a residual that has produced three.
!(select 5 (nats 0))
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 3 (search-policy (control FIFO (retain True))) (nats 0))
   (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
     (select 2 (search-policy (control FIFO (demand additional) (retain True))) $r)
     (Pair $prefix (Pair $rest $c2))))
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 3 (search-policy (control FIFO (retain True))) (nats 0))
   (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
     (select 2 (search-policy (control FIFO (retain True))) $r)
     (Pair $prefix (Pair $rest $s2))))

; 5. A demand of zero answers inspects nothing.
!(select 0 (nats 0))

; 6. `once` stops before the alternatives after its answer: their world
;    writes never happen.
(= (attempt) (let $u (add-atom &self (tried a)) (empty)))
(= (attempt) (let $u (add-atom &self (tried b)) done))
(= (attempt) (let $u (add-atom &self (tried c)) late))
!(once (attempt))
!(match &self (tried $x) $x)
