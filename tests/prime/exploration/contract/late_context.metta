; Two searches that read the world: one reads once and answers twice, the
; other reads again for each answer.  Every whole run gives both the same
; answers.  An observer that pauses after the first answer, changes the world,
; and resumes would tell them apart: the first would keep the value it read,
; the second would read the new world.  A retained residual resumes only in
; the world it paused in, so both resumptions are refused, and the two
; searches stay equal for every observer that pauses and resumes.  This is
; the delimited-commitment core's theorem for read-only searches under
; currency-checked resumption (Machines/ScopedCommit:
; resumeIfCurrent_contextual), and its controls (late_world_separates,
; currency_check_refuses_changed_world).

(cfg 1)
(= (readOne) (match &self (cfg $x) $x))
(= (twice $v) $v)
(= (twice $v) $v)
(= (readOnce) (let $v (readOne) (twice $v)))
(= (readEach) (readOne))
(= (readEach) (readOne))

; 1. Whole runs agree.
!(collapse (readOnce))
!(collapse (readEach))

; 2. Pause after one answer and resume in the same world: they agree.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (readOnce))
   (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
     (Pair $p (Pair $rest $t2))))
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (readEach))
   (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
     (Pair $p (Pair $rest $t2))))

; 3. Pause, change the world, resume: both resumptions are refused.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (readOnce))
   (let $u (remove-atom &self (cfg 1))
     (let $u2 (add-atom &self (cfg 2))
       (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
         (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
         (Pair $p (Pair $rest $t2))))))
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (readEach))
   (let $u (remove-atom &self (cfg 2))
     (let $u2 (add-atom &self (cfg 3))
       (let (Observation $rest (satisfied $s2) (closed $c2) (residual $r2) (work $w2) (status $t2))
         (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
         (Pair $p (Pair $rest $t2))))))
