; What each approximate-adequacy certificate sees on the road map.
;
; The world is the road map under revision: four towns, major roads a-b and
; b-d, minor roads a-c and c-d.  Four stages matter: every road open
; (allOpen), only the major roads open (majorOnly), and each of these after
; road b-d receives a closed report (detour, blocked).  "d is reachable from
; a" holds at allOpen, majorOnly and detour and fails at blocked
; (answer_allOpen, answer_majorOnly, answer_detour, answer_blocked).  Closing
; b-d sends allOpen to detour and majorOnly to blocked; detour and blocked
; stay where they are (Stage.close).
;
; The model under test keeps the present answer: its state is the answer,
; and it predicts that no revision changes it.  Four certificates are
; computed in exact rational arithmetic, and then the one-step update square:
;   correspondence defect  the model's update for a composite revision
;                          against the composite of its updates; it never
;                          compares the model with the world;
;   goal-weighted average  a weighted average of those defects;
;   Girard-Pappas          approximate bisimulation of the deterministic
;                          revision, b-d closing at every step; for
;                          deterministic systems it is closeness of the
;                          observation runs (approxBisimilar_update_iff);
;   bisimulation metric    the discounted behavioural pseudometric of
;                          labelled Markov chains: b-d closes with probability
;                          r = 1/2 per step, the model's chain is a point mass,
;                          discount c = 1/2, observable 1 for "reachable".
;
; The metric is computed as the least m with m >= |observation gap| and
; m >= c * cost(coupling, m) at every pair and action: the least coupling
; bound.  Where one side is a point mass the coupling is forced (the
; independent one), so the least solution is the metric itself.  A pair's own
; weight is solved for exactly, and every other successor pair is computed
; first; every chain here returns to a pair only through its own self-loop.
; Separately, the certificate table keepBound (0 at allOpen/true,
; detour/true and blocked/false, 1/3 at majorOnly/true, 1 elsewhere) is
; checked as a coupling bound: nonnegative, above every observation gap, and
; for every action a coupling whose discounted cost stays below it.
;
; Mirrors: keepBound_couplingBound, metric_allOpen, metric_majorOnly,
; bisimulationMetric_le, mul_kantorovich_bisimulationMetric_le,
; kantorovich_of_eq_dirac_right, allOpen_approxBisimilar,
; majorOnly_not_approxBisimilar, approxBisimilar_update_iff,
; keepCorrespondence_exact, keep_averageDefect, keep_square,
; keep_not_square.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

!(pragma! prefer-rationals True)

; ---- exact arithmetic ---------------------------------------------------------

(= (abs-q $x) (if (< $x 0) (- 0 $x) $x))
(= (max-q $x $y) (if (< $x $y) $y $x))
(= (in-list $x $xs)
   (if (== $xs ()) False
       (let $h (car-atom $xs)
         (if (== $h $x) True (let $t (cdr-atom $xs) (in-list $x $t)){$t})){$h}))

; ---- the world and the model -----------------------------------------------------

(= (answer allOpen) True)
(= (answer majorOnly) True)
(= (answer detour) True)
(= (answer blocked) False)
(= (answer-value $s) (if (answer $s) 1 0))
(= (belief-value $b) (if $b 1 0))
(= (close-bd allOpen) detour)
(= (close-bd majorOnly) blocked)
(= (close-bd detour) detour)
(= (close-bd blocked) blocked)

(= (close-rate) 1/2)
(= (discount) 1/2)

; The world chain: one action, a revision step.  A distribution is a list of
; (state weight) pairs.
(= (states world) (allOpen majorOnly detour blocked))
(= (actions world) (revise))
(= (obs world $s) (answer-value $s))
(= (trans world revise allOpen)
   (let* (($r (close-rate)) ($k (- 1 $r))) ((detour $r) (allOpen $k))))
(= (trans world revise majorOnly)
   (let* (($r (close-rate)) ($k (- 1 $r))) ((blocked $r) (majorOnly $k))))
(= (trans world revise detour) ((detour 1)))
(= (trans world revise blocked) ((blocked 1)))
; The keep model: its state is the answer, and it never changes.
(= (states keep) (True False))
(= (obs keep $b) (belief-value $b))
(= (trans keep revise $b) (($b 1)))

; ---- couplings and coupling bounds ---------------------------------------------

(= (gap $P $Q $s $t) (let* (($x (obs $P $s)) ($y (obs $Q $t))) (abs-q (- $x $y))))
(= (independent $mu $nu)
   (collapse (let* ((($x $wx) (superpose $mu)) (($y $wy) (superpose $nu)))
               (($x $y) (* $wx $wy)))))
; The coupling used at each pair: with a point mass on one side it is forced.
(= (coupling $P $Q $a $s $t)
   (let* (($mu (trans $P $a $s)) ($nu (trans $Q $a $t))) (independent $mu $nu)))

(= (row-sum $w $x)
   (foldl-atom $w 0 $acc $e (let (($u $v) $m) $e (if (== $u $x) (+ $acc $m) $acc))))
(= (column-sum $w $y)
   (foldl-atom $w 0 $acc $e (let (($u $v) $m) $e (if (== $v $y) (+ $acc $m) $acc))))
(= (keys $mu) (map-atom $mu $e (let ($x $m) $e $x)))
; A coupling of mu and nu: nonnegative weights on pairs of their states, with
; marginals mu and nu.
(= (is-coupling $w $mu $nu)
   (let* (($xs (keys $mu)) ($ys (keys $nu))
          ($bad-entries (collapse (let (($u $v) $m) (superpose $w)
                                    (if (and (<= 0 $m) (and (in-list $u $xs) (in-list $v $ys)))
                                        (empty) $u)){$u $v $m}))
          ($bad-rows (collapse (let ($x $m) (superpose $mu)
                                 (if (== (row-sum $w $x) $m) (empty) $x)){$x $m}))
          ($bad-columns (collapse (let ($y $m) (superpose $nu)
                                    (if (== (column-sum $w $y) $m) (empty) $y)){$y $m})))
     (and (== $bad-entries ()) (and (== $bad-rows ()) (== $bad-columns ())))))

(= (cost $w $table)
   (foldl-atom $w 0 $acc $e
     (let (($x $y) $m) $e (+ $acc (* $m (bound-value $table $x $y))))))

; A coupling bound at discount c: nonnegative, above every observation gap,
; and for every action a coupling whose discounted cost stays below it.
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
              (and (<= (* $c $k) $v) (actions-ok $P $Q $c $table $s $t $v $rest)))){$a $rest $w $mu $nu $k}))
(= (coupling-bound $P $Q $c $table)
   (let* (($ss (states $P)) ($ts (states $Q))
          ($bad (collapse (let* (($s (superpose $ss)) ($t (superpose $ts)))
                            (if (bound-ok $P $Q $c $table $s $t) (empty) ($s $t))){$s $t})))
     (== $bad ())))

; ---- the least coupling bound ----------------------------------------------------

(= (least-bound $P $Q $c $s $t)
   (let* (($g (gap $P $Q $s $t)) ($acts (actions $P))
          ($terms (map-atom $acts $a (action-term $P $Q $c $a $s $t))))
     (foldl-atom $terms $g $acc $x (max-q $acc $x))))
; For one action: the least m with m >= c * (self * m + rest), where self is
; the weight the coupling puts on the pair itself.
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

; ---- the tables ------------------------------------------------------------------

; keepBound: the certificate of the metric at c = r = 1/2.
(= (keep-bound-entries)
   (((allOpen True) 0) ((detour True) 0) ((majorOnly True) 1/3) ((blocked False) 0)))
; A table that claims less at majorOnly.
(= (too-small-entries)
   (((allOpen True) 0) ((detour True) 0) ((majorOnly True) 1/4) ((blocked False) 0)))
(= (lookup-entry $entries $s $t $default)
   (if (== $entries ()) $default
       (let* ((($pair $v) (car-atom $entries)) ($rest (cdr-atom $entries)))
         (if (== $pair ($s $t)) $v (lookup-entry $rest $s $t $default))){$pair $v $rest}))
(= (bound-value keep-bound $s $t) (let $es (keep-bound-entries) (lookup-entry $es $s $t 1)))
(= (bound-value too-small $s $t) (let $es (too-small-entries) (lookup-entry $es $s $t 1)))

; ---- (c) the bisimulation metric ---------------------------------------------------

; 1. keepBound is a coupling bound at discount 1/2 (keepBound_couplingBound).
!(let $c (discount) (coupling-bound world keep $c keep-bound))
; 2. The metric is 0 at allOpen: the kept answer stays right forever
;    (metric_allOpen) ...
!(let $c (discount) (least-bound world keep $c allOpen True))
; 3. ... and exactly 1/3 at majorOnly, the solution of d = c (r + (1 - r) d):
;    the present answers agree, the futures do not, and the metric weighs the
;    difference by its probability and its delay (metric_majorOnly).
!(let $c (discount) (least-bound world keep $c majorOnly True))
; 4. The certificate is tight there: its entry is the metric
;    (metric_majorOnly, whose upper half is keepBound_couplingBound) ...
!(let* (($c (discount)) ($m (least-bound world keep $c majorOnly True)))
   (== $m (bound-value keep-bound majorOnly True)))
; 5. ... so a table that claims 1/4 there is not a coupling bound: the metric
;    lies below every coupling bound (bisimulationMetric_le, metric_majorOnly).
!(let $c (discount) (coupling-bound world keep $c too-small))

; ---- (b) Girard-Pappas: the deterministic revision ---------------------------------

; For deterministic systems an approximate bisimulation at precision eps
; relates s and b exactly when every observation along the two runs is within
; eps (approxBisimilar_update_iff).  The run is followed until a pair repeats;
; the drift is the largest gap on it.
(= (drift $s $b $seen)
   (if (in-list ($s $b) $seen) 0
       (let* (($x (answer-value $s)) ($y (belief-value $b)) ($here (abs-q (- $x $y)))
              ($s2 (close-bd $s)) ($seen2 (cons-atom ($s $b) $seen))
              ($later (drift $s2 $b $seen2)))
         (max-q $here $later)){$x $y $here $s2 $seen2 $later}))
(= (approx-bisimilar $s $b $eps) (let $d (drift $s $b ()) (<= $d $eps)))

; 6. At allOpen the drift is 0 (allOpen_approxBisimilar); at majorOnly it is
;    1: one closing makes d unreachable while the model still says reachable.
!(drift allOpen True ())
!(drift majorOnly True ())
; 7. So no approximate bisimulation below 1 relates majorOnly to the model
;    (majorOnly_not_approxBisimilar), checked at three precisions.
!(map-atom (0 1/2 99/100) $eps (approx-bisimilar majorOnly True $eps))

; ---- (a) and (f): the correspondence defect and its goal-weighted average -------------

; A revision process is a number of closed reports on b-d; the model maps each
; process to the identity on answers.  The defect of a composable pair (m then
; n closings) at a stage is measured at the stage's answer: the model of the
; composite against the composite of the models.
(= (keep-update $n $v) $v)
(= (pair-defect $s $m $n)
   (let* (($v (answer-value $s)) ($mn (+ $m $n))
          ($whole (keep-update $mn $v)) ($first (keep-update $m $v)) ($then (keep-update $n $first)))
     (abs-q (- $whole $then))))
(= (sample-pairs)
   ((allOpen 0 1) (allOpen 1 1) (allOpen 2 1) (allOpen 1 2)
    (majorOnly 0 1) (majorOnly 1 1) (majorOnly 2 1) (majorOnly 1 2)))
(= (defects) (let $ps (sample-pairs) (map-atom $ps $p (let ($s $m $n) $p (pair-defect $s $m $n)))))
; A weighting of the sample: weights summing to one.
(= (weighted $weights)
   (let $ds (defects) (weighted-sum $weights $ds)))
(= (weighted-sum $ws $ds)
   (if (== $ws ()) 0
       (let* (($w (car-atom $ws)) ($d (car-atom $ds)) ($ws2 (cdr-atom $ws)) ($ds2 (cdr-atom $ds))
              ($rest (weighted-sum $ws2 $ds2)))
         (+ (* $w $d) $rest)){$w $d $ws2 $ds2 $rest}))

; 8. The keep model is a functor on the revision processes: every defect is 0,
;    at majorOnly as at allOpen (keepCorrespondence_exact).
!(let $ds (defects) (foldl-atom $ds 0 $acc $d (max-q $acc $d)))
; 9. So every goal-weighted average of defects is 0: uniform on the sample, and
;    all weight on the pairs at majorOnly (keep_averageDefect).
!(weighted (1/8 1/8 1/8 1/8 1/8 1/8 1/8 1/8))
!(weighted (0 0 0 0 1/4 1/4 1/4 1/4))

; ---- (e) the update square ----------------------------------------------------------

; 10. The square of the keep update against closing b-d errs by 1, attained
;     at majorOnly (keep_square, keep_not_square).
!(let $ss (states world)
   (foldl-atom $ss 0 $acc $s
     (let* (($after (close-bd $s)) ($x (answer-value $after)) ($y (answer-value $s)))
       (max-q $acc (abs-q (- $x $y))))))
