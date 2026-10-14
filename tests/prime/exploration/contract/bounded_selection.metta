; A bounded selection observes a prefix of the answers and performs only the
; work those answers need: the alternatives after the selected answers never
; run, so their world writes never happen, and a producer with infinitely
; many answers stops at the bound (Machines/ScopedCommit: select_then_resume,
; run_once_eq_select, once_stops_before_losing_effect).  Without a retained
; residual the rest of the search is discarded, as by `once`.

(= (gen $tag) (let $u (add-atom &self (made $tag 1)) 1))
(= (gen $tag) (let $u (add-atom &self (made $tag 2)) 2))
(= (gen $tag) (let $u (add-atom &self (made $tag 3)) 3))
(= (gen $tag) (let $u (add-atom &self (made $tag 4)) 4))
(= (enats $tag $n) (let $u (add-atom &self (made $tag $n)) $n))
(= (enats $tag $n) (enats $tag (+ $n 1)))

; 1. Two answers, and the writes of those two alone.
!(select 2 (gen two))
!(match &self (made two $x) $x)

; 2. Three answers.
!(select 3 (gen three))
!(match &self (made three $x) $x)

; 3. A producer with infinitely many answers, each of which writes, stops at
;    its second answer.
!(select 2 (enats inf 0))
!(match &self (made inf $x) $x)

; 4. `once` is the selection of one answer; a selection inside `once` keeps
;    its own bound.
!(once (gen one))
!(match &self (made one $x) $x)
!(once (select 2 (gen inner)))
!(match &self (made inner $x) $x)

; 5. Without a bound every alternative runs, and every write happens.
!(collapse (gen all))
!(match &self (made all $x) $x)

; 6. A selection that names its control and retains its residual admits a
;    producer with world effects.  Its residual keeps the world position it
;    paused at, its own writes included; resuming it there gives the rest of
;    the run, every answer and every write once.  Under FIFO control the
;    alternatives are interleaved, so the pause can come after the writes of
;    alternatives whose answers are still pending.
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 2 (search-policy (control FIFO (retain True))) (gen kept))
   (let $seen (collapse (match &self (made kept $x) $x))
     (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
       (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
       (Paused $prefix $t $seen Resumed $rest $t2))))
!(match &self (made kept $x) $x)

; 7. A write by anyone else between the pause and the resumption leaves the
;    residual in a world it did not see: the resumption is refused.
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 2 (search-policy (control FIFO (retain True))) (gen stale))
   (let $u (add-atom &self (foreign write))
     (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
       (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
       (Pair $prefix (Pair $rest $t2)))))
!(match &self (made stale $x) $x)
