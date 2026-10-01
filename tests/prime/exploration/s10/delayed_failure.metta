; Discounting forgives a delayed failure.
;
; A deterministic world raises an alarm two steps after the start (start ->
; middle -> fail, and fail stays); the model never does (one state, quiet).
; The observable is the alarm: 1 at fail, 0 elsewhere.  Both chains have one
; action, tick, and every transition is a point mass.
;
; The bisimulation metric at the start is pinned between two computations:
;   above  the least coupling bound: the least m with m >= |alarm gap| and
;          m >= c * m(next pair) (on deterministic chains the only coupling is
;          the point mass, couplingBound_ofFunction_iff); a pair's own weight
;          is solved for exactly.  Separately, the certificate table (c^2 at
;          start, c at middle, 1 at fail) is checked as a coupling bound;
;   below  the functional expression "the alarm two steps ahead",
;          next (next (observe)), which is c^2 at the start and 0 in the model.
; They meet at c^2: 1/4 at c = 1/2.  An error n steps ahead costs c^n.
;
; Girard-Pappas approximate bisimulation of the two deterministic runs is
; closeness of their observations at every step (approxBisimilar_update_iff):
; the drift is the largest gap along the runs, followed until a pair repeats.
; At the second step the gap is 1, whatever the delay.  At discount 1 the two
; certificates are the same distance on deterministic chains
; (bisimulationMetric_ofFunction_le_iff).
;
; Mirrors: couplingBound, couplingBound_ofFunction_iff, eval_alarmAhead_start,
; eval_alarmAhead_calm, abs_eval_sub_le_bisimulationMetric, metric_start,
; metric_start_half, not_approxBisimilar, approxBisimilar_update_iff,
; bisimulationMetric_ofFunction_le_iff.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

!(pragma! prefer-rationals True)

; ---- exact arithmetic ---------------------------------------------------------

(= (abs-q $x) (if (< $x 0) (- 0 $x) $x))
(= (max-q $x $y) (if (< $x $y) $y $x))
(= (in-list $x $xs)
   (if (== $xs ()) False
       (let $h (car-atom $xs)
         (if (== $h $x) True (let $t (cdr-atom $xs) (in-list $x $t))))))

; ---- the chains ------------------------------------------------------------------

(= (states world) (start middle fail))
(= (actions world) (tick))
(= (next-stage start) middle)
(= (next-stage middle) fail)
(= (next-stage fail) fail)
(= (trans world tick $s) (let $n (next-stage $s) (($n 1))))
(= (obs world $s) (if (== $s fail) 1 0))
(= (states calm) (quiet))
(= (trans calm tick quiet) ((quiet 1)))
(= (obs calm quiet) 0)

; ---- couplings and coupling bounds ---------------------------------------------

(= (gap $P $Q $s $t) (let* (($x (obs $P $s)) ($y (obs $Q $t))) (abs-q (- $x $y))))
(= (independent $mu $nu)
   (collapse (let* ((($x $wx) (superpose $mu)) (($y $wy) (superpose $nu)))
               (($x $y) (* $wx $wy)))))
(= (coupling $P $Q $a $s $t)
   (let* (($mu (trans $P $a $s)) ($nu (trans $Q $a $t))) (independent $mu $nu)))

(= (row-sum $w $x)
   (foldl-atom $w 0 $acc $e (let (($u $v) $m) $e (if (== $u $x) (+ $acc $m) $acc))))
(= (column-sum $w $y)
   (foldl-atom $w 0 $acc $e (let (($u $v) $m) $e (if (== $v $y) (+ $acc $m) $acc))))
(= (keys $mu) (map-atom $mu $e (let ($x $m) $e $x)))
(= (is-coupling $w $mu $nu)
   (let* (($xs (keys $mu)) ($ys (keys $nu))
          ($bad-entries (collapse (let (($u $v) $m) (superpose $w)
                                    (if (and (<= 0 $m) (and (in-list $u $xs) (in-list $v $ys)))
                                        (empty) $u))))
          ($bad-rows (collapse (let ($x $m) (superpose $mu)
                                 (if (== (row-sum $w $x) $m) (empty) $x))))
          ($bad-columns (collapse (let ($y $m) (superpose $nu)
                                    (if (== (column-sum $w $y) $m) (empty) $y)))))
     (and (== $bad-entries ()) (and (== $bad-rows ()) (== $bad-columns ())))))

(= (cost $w $table)
   (foldl-atom $w 0 $acc $e
     (let (($x $y) $m) $e (+ $acc (* $m (bound-value $table $x $y))))))
(= (bound-ok $P $Q $c $table $s $t)
   (let* (($v (bound-value $table $s $t)) ($g (gap $P $Q $s $t)) ($acts (actions $P)))
     (and (<= 0 $v) (and (<= $g $v) (actions-ok $P $Q $c $table $s $t $v $acts)))))
(= (actions-ok $P $Q $c $table $s $t $v $acts)
   (if (== $acts ()) True
       (let* (($a (car-atom $acts)) ($rest (cdr-atom $acts))
              ($w (coupling $P $Q $a $s $t))
              ($mu (trans $P $a $s)) ($nu (trans $Q $a $t))
              ($k (cost $w $table)))
         (and (is-coupling $w $mu $nu)
              (and (<= (* $c $k) $v) (actions-ok $P $Q $c $table $s $t $v $rest))))))
(= (coupling-bound $P $Q $c $table)
   (let* (($ss (states $P)) ($ts (states $Q))
          ($bad (collapse (let* (($s (superpose $ss)) ($t (superpose $ts)))
                            (if (bound-ok $P $Q $c $table $s $t) (empty) ($s $t))))))
     (== $bad ())))

; The certificate table: c^2 at the start, c in the middle, 1 at the failure.
(= (bound-value (table $c) start $t) (* $c $c))
(= (bound-value (table $c) middle $t) $c)
(= (bound-value (table $c) fail $t) 1)

; ---- above: the least coupling bound ----------------------------------------------

(= (least-bound $P $Q $c $s $t)
   (let* (($g (gap $P $Q $s $t)) ($acts (actions $P))
          ($terms (map-atom $acts $a (action-term $P $Q $c $a $s $t))))
     (foldl-atom $terms $g $acc $x (max-q $acc $x))))
(= (action-term $P $Q $c $a $s $t)
   (let* (($w (coupling $P $Q $a $s $t))
          ($self (self-weight $w $s $t))
          ($rest (other-cost $P $Q $c $w $s $t))
          ($k (* $c $self)))
     (if (== $k 1) 0 (/ (* $c $rest) (- 1 $k)))))
(= (self-weight $w $s $t)
   (foldl-atom $w 0 $acc $e
     (let (($x $y) $m) $e (if (and (== $x $s) (== $y $t)) (+ $acc $m) $acc))))
(= (other-cost $P $Q $c $w $s $t)
   (foldl-atom $w 0 $acc $e
     (let (($x $y) $m) $e
       (if (and (== $x $s) (== $y $t)) $acc
           (if (== $m 0) $acc (+ $acc (* $m (least-bound $P $Q $c $x $y))))))))

; ---- below: "the alarm two steps ahead" ----------------------------------------------

(= (ev $P $c (observe) $s) (obs $P $s))
(= (ev $P $c (next $a $phi) $s)
   (let $mu (trans $P $a $s)
     (* $c (foldl-atom $mu 0 $acc $e (let ($x $m) $e (+ $acc (* $m (ev $P $c $phi $x))))))))
(= (alarm-ahead) (next tick (next tick (observe))))
(= (pinned-metric $c)
   (let* (($phi (alarm-ahead))
          ($above (least-bound world calm $c start quiet))
          ($x (ev world $c $phi start)) ($y (ev calm $c $phi quiet)) ($below (abs-q (- $x $y))))
     (if (== $above $below) $above (not-pinned $above $below))))

; ---- the metric ------------------------------------------------------------------------

; 1. The certificate table is a coupling bound at c = 1/2 (couplingBound).
!(coupling-bound world calm 1/2 (table 1/2))
; 2. The metric at the start is exactly c^2 = 1/4 at c = 1/2 (metric_start_half,
;    eval_alarmAhead_start, eval_alarmAhead_calm).
!(pinned-metric 1/2)
; 3. It is c^2 at every discount: 1 at c = 1, 1/4 at c = 1/2, 1/100 at
;    c = 1/10 (metric_start).  The failure two steps ahead costs c^2.
!(map-atom (1 1/2 1/10) $c (pinned-metric $c))

; ---- Girard-Pappas ---------------------------------------------------------------------

(= (drift $s $t $seen)
   (if (in-list ($s $t) $seen) 0
       (let* (($x (obs world $s)) ($y (obs calm $t)) ($here (abs-q (- $x $y)))
              ($s2 (next-stage $s)) ($seen2 (cons-atom ($s $t) $seen))
              ($later (drift $s2 $t $seen2)))
         (max-q $here $later))))
(= (approx-bisimilar $eps) (let $d (drift start quiet ()) (<= $d $eps)))

; 4. The drift from the start is 1, reached at the second step: no
;    approximate bisimulation below 1 relates the start to the model
;    (not_approxBisimilar), checked at three precisions.
!(drift start quiet ())
!(map-atom (0 1/2 99/100) $eps (approx-bisimilar $eps))
; 5. At discount 1 the metric is the drift (bisimulationMetric_ofFunction_le_iff,
;    metric_start at c = 1); below discount 1 it is smaller (metric_start_half).
!(let* (($d (drift start quiet ())) ($m (pinned-metric 1)) ($h (pinned-metric 1/2)))
   ((== $m $d) (< $h $d)))
