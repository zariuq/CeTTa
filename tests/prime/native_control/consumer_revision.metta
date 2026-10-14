(= (items) 1)
(= (items) 2)
(= (loop) (loop))
(= (late-true $n) (if (== $n 0) True (late-true (- $n 1))))

; A withdrawn divergent predicate cannot prevent the replacement from closing.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (goal $xs (loop)) (budget 100))) (items))
   (select 1 (search-policy (control FIFO (goal $xs False) (receipts True) (budget 100))) $r))

; A late True belongs to the withdrawn generation, never the new False goal.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (goal $xs (late-true 1000)) (budget 100))) (items))
   (select 1 (search-policy (control FIFO (goal $xs False) (receipts True) (budget 100))) $r))

; Obsolete cost computations cannot increment the new cost-generation count.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (best $x (loop)) (budget 100))) (items))
   (select 1 (search-policy (control FIFO (best $x $x) (receipts True) (budget 100))) $r))

; A new goal rechecks retained witnesses and may select a different collection.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (goal $xs True) (retain True))) (items))
   (select 1 (search-policy (control FIFO (goal (2) True) (receipts True) (budget 100))) $r))

; Pure metadata cannot spend another scope's owned continuation capability.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 0 (search-policy (control FIFO (retain True))) (items))
   (Pair
     (select 1 (search-policy (control WO
       (key $candidate (select 1 (search-policy (control FIFO)) $r)) (budget 100))) (items))
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)))
