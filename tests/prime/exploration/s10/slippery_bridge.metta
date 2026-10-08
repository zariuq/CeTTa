; What each approximate-adequacy certificate sees on a slippery bridge.
;
; A delivery leaves the depot for the garden, over a wooden bridge or over a
; hill (actions viaBridge and viaHill, chosen at the depot).  On the bridge it
; slips with probability p and ends in the ditch (slipped); the hill always
; succeeds; garden and slipped are absorbing.  The observable is "delivered":
; 1 in the garden, 0 elsewhere.  The world slips with p = 3/10.  A model is the
; same chain with slip probability q; the discount is c = 1/2.
;   rare slips   q = 1/100
;   never slips  q = 0
;   the cache    q = 3/10, the world's own step model, plus a stale estimate
;                of 9/10 for the whole bridge route
;
; The bisimulation metric at the depot is pinned between two computations:
;   above  the least coupling bound for a declared coupling at each pair and
;          action: the independent coupling, except on the bridge, where
;          deliveries are matched with deliveries, slips with slips, and the
;          world's excess slip mass p - q is moved onto the model's delivery
;          (slipCoupling).  A pair's own weight is solved for exactly; every
;          other successor pair is computed first.  Separately, the
;          certificate table (c^2 (p - q) at depot/depot, c (p - q) at
;          bridge/bridge, 0 on the other diagonal pairs, 1 elsewhere) is
;          checked as a coupling bound;
;   below  the functional expression "deliver over the bridge", two discounted
;          steps along the bridge route and then the observable, whose values
;          at the two depots differ by at most the metric.
; They meet at c^2 (p - q).
;
; Girard-Pappas approximate bisimulation is applied to the supports of the
; transitions (the successors of positive probability).  The largest
; approximate bisimulation at precision eps is computed as a greatest fixed
; point: start from the pairs whose observations are within eps, remove pairs
; whose moves cannot be matched inside the relation, until nothing changes.
;
; The correspondence defect is computed on the route graph (edges depot-bridge,
; bridge-garden, bridge-slipped, depot-hill, hill-garden): a model maps a path
; to its probability, and the defect of a composable pair of paths is the
; distance between the model of the composite and the product of the models.
;
; Mirrors: couplingBound, metric_depot, metric_depot_rare, metric_depot_never,
; eval_bridgeDelivery, abs_eval_sub_le_bisimulationMetric, supports_exact,
; supports_far, pathCorrespondence_exact, cachedCorrespondence_defect,
; cachedCorrespondence_hill_defect,
; cache_defect_step_exact, coherent_defect_zero_metric_positive,
; today_average_zero, tomorrow_average, cached_conclusion_fails.
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

; ---- the chains ------------------------------------------------------------------

(= (world-slip) 3/10)
(= (discount) 1/2)

; (delivery p): the delivery chain with slip probability p.
(= (states (delivery $p)) (depot bridge hill garden slipped))
(= (actions (delivery $p)) (viaBridge viaHill))
(= (obs (delivery $p) $x) (if (== $x garden) 1 0))
(= (trans (delivery $p) $a $s) (step-dist $p $a $s))
(= (step-dist $p viaBridge depot) ((bridge 1)))
(= (step-dist $p viaHill depot) ((hill 1)))
(= (step-dist $p $a bridge) (let $k (- 1 $p) ((garden $k) (slipped $p))))
(= (step-dist $p $a hill) ((garden 1)))
(= (step-dist $p $a garden) ((garden 1)))
(= (step-dist $p $a slipped) ((slipped 1)))

; ---- couplings and coupling bounds ---------------------------------------------

(= (gap $P $Q $s $t) (let* (($x (obs $P $s)) ($y (obs $Q $t))) (abs-q (- $x $y))))
(= (independent $mu $nu)
   (collapse (let* ((($x $wx) (superpose $mu)) (($y $wy) (superpose $nu)))
               (($x $y) (* $wx $wy)))))
; On the bridge: deliver together, slip together, and move the excess slip
; mass of the world onto delivery in the model.
(= (slip-coupling $p $q)
   (let* (($d (- 1 $p)) ($e (- $p $q)))
     (((garden garden) $d) ((slipped slipped) $q) ((slipped garden) $e))))
(= (coupling (delivery $p) (delivery $q) $a $s $t)
   (if (and (== $s bridge) (== $t bridge))
       (slip-coupling $p $q)
       (let* (($mu (step-dist $p $a $s)) ($nu (step-dist $q $a $t))) (independent $mu $nu)){$mu $nu}))

(= (row-sum $w $x)
   (foldl-atom $w 0 $acc $e (let (($u $v) $m) $e (if (== $u $x) (+ $acc $m) $acc))))
(= (column-sum $w $y)
   (foldl-atom $w 0 $acc $e (let (($u $v) $m) $e (if (== $v $y) (+ $acc $m) $acc))))
(= (keys $mu) (map-atom $mu $e (let ($x $m) $e $x)))
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

; The certificate table of the pinned metric.
(= (bound-value (table $c $p $q) $s $t)
   (if (and (== $s depot) (== $t depot)) (* $c (* $c (- $p $q)))
       (if (and (== $s bridge) (== $t bridge)) (* $c (- $p $q))
           (if (== $s $t) 0 1))))

; ---- above: the least coupling bound for the declared couplings ---------------------

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

; ---- below: functional expressions ---------------------------------------------------

; (observe) is the observable; (next a phi) is the discounted expectation of
; phi after action a.
(= (ev $P $c (observe) $s) (obs $P $s))
(= (ev $P $c (next $a $phi) $s)
   (let $mu (trans $P $a $s)
     (* $c (foldl-atom $mu 0 $acc $e (let ($x $m) $e (+ $acc (* $m (ev $P $c $phi $x))))))))
(= (bridge-delivery) (next viaBridge (next viaBridge (observe))))
(= (expression-gap $P $Q $c $phi $s $t)
   (let* (($x (ev $P $c $phi $s)) ($y (ev $Q $c $phi $t))) (abs-q (- $x $y))))

; The metric at the depots, when the two computations meet.
(= (pinned-metric $p $q)
   (let* (($c (discount)) ($phi (bridge-delivery))
          ($above (least-bound (delivery $p) (delivery $q) $c depot depot))
          ($below (expression-gap (delivery $p) (delivery $q) $c $phi depot depot)))
     (if (== $above $below) $above (not-pinned $above $below))))

; ---- the bisimulation metric -------------------------------------------------------

; 1. The certificate table is a coupling bound, for rare slips and for never
;    slipping (couplingBound).
!(let* (($p (world-slip)) ($c (discount))) (coupling-bound (delivery $p) (delivery 1/100) $c (table $c $p 1/100)))
!(let* (($p (world-slip)) ($c (discount))) (coupling-bound (delivery $p) (delivery 0) $c (table $c $p 0)))
; 2. The metric at the depot is c^2 (p - q) (metric_depot, eval_bridgeDelivery):
;    29/400 for rare slips (metric_depot_rare), 3/40 = c^2 p for never
;    slipping (metric_depot_never), and 0 for the world's own step model
;    (cache_defect_step_exact).  It varies continuously with q.
!(let $p (world-slip) (pinned-metric $p 1/100))
!(let $p (world-slip) (pinned-metric $p 0))
!(let $p (world-slip) (pinned-metric $p $p))

; ---- Girard-Pappas on the supports ---------------------------------------------------

(= (support $P $a $s)
   (let $mu (trans $P $a $s) (collapse (let ($x $m) (superpose $mu) (if (== $m 0) (empty) $x)))))
(= (some-related $xs $t2s $rel)
   (if (== $xs ()) False
       (let* (($x (car-atom $xs)) ($rest (cdr-atom $xs)))
         (if (any-related $x $t2s $rel) True (some-related $rest $t2s $rel))){$x $rest}))
(= (any-related $x $ys $rel)
   (if (== $ys ()) False
       (let* (($y (car-atom $ys)) ($rest (cdr-atom $ys)))
         (if (in-list ($x $y) $rel) True (any-related $x $rest $rel))){$y $rest}))
(= (all-matched-forth $xs $ys $rel)
   (if (== $xs ()) True
       (let* (($x (car-atom $xs)) ($rest (cdr-atom $xs)))
         (and (any-related $x $ys $rel) (all-matched-forth $rest $ys $rel))){$x $rest}))
(= (any-related-back $y $xs $rel)
   (if (== $xs ()) False
       (let* (($x (car-atom $xs)) ($rest (cdr-atom $xs)))
         (if (in-list ($x $y) $rel) True (any-related-back $y $rest $rel))){$x $rest}))
(= (all-matched-back $ys $xs $rel)
   (if (== $ys ()) True
       (let* (($y (car-atom $ys)) ($rest (cdr-atom $ys)))
         (and (any-related-back $y $xs $rel) (all-matched-back $rest $xs $rel))){$y $rest}))
; Every move of s is matched by a move of t into the relation, and back.
(= (matched $P $Q $a $rel $s $t)
   (let* (($xs (support $P $a $s)) ($ys (support $Q $a $t)))
     (and (all-matched-forth $xs $ys $rel) (all-matched-back $ys $xs $rel))))
(= (refine $P $Q $a $rel)
   (let $kept (collapse (let ($s $t) (superpose $rel)
                          (if (matched $P $Q $a $rel $s $t) ($s $t) (empty))))
     (if (== $kept $rel) $rel (refine $P $Q $a $kept))))
(= (largest-bisimulation $P $Q $a $eps)
   (let* (($ss (states $P)) ($ts (states $Q))
          ($close (collapse (let* (($s (superpose $ss)) ($t (superpose $ts)))
                              (if (<= (gap $P $Q $s $t) $eps) ($s $t) (empty))){$s $t})))
     (refine $P $Q $a $close)))
(= (supports-bisimilar $p $q $a $eps)
   (let $rel (largest-bisimulation (delivery $p) (delivery $q) $a $eps) (in-list (depot depot) $rel)))

; 3. Rare slips: the supports are the world's, so they are bisimilar at
;    precision 0, for both actions (supports_exact), although the predicted
;    delivery is off by p - q: Girard-Pappas calls this model exact.
!(let $p (world-slip) (map-atom (viaBridge viaHill) $a (supports-bisimilar $p 1/100 $a 0)))
; 4. Never slipping: no approximate bisimulation of the supports below 1
;    (supports_far): the world's slip must be matched by a delivery.  Its
;    predictions are off by only p, and Girard-Pappas calls it maximally wrong.
!(let $p (world-slip) (map-atom (0 1/2 99/100) $eps (supports-bisimilar $p 0 viaBridge $eps)))

; ---- the correspondence defect on the route graph ------------------------------------

(= (edge-prob $q depotBridge) 1)
(= (edge-prob $q bridgeGarden) (- 1 $q))
(= (edge-prob $q bridgeSlipped) $q)
(= (edge-prob $q depotHill) 1)
(= (edge-prob $q hillGarden) 1)
; A path is (start end edges).
(= (paths)
   ((depot depot ()) (bridge bridge ()) (hill hill ()) (garden garden ()) (slipped slipped ())
    (depot bridge (depotBridge)) (bridge garden (bridgeGarden)) (bridge slipped (bridgeSlipped))
    (depot hill (depotHill)) (hill garden (hillGarden))
    (depot garden (depotBridge bridgeGarden)) (depot slipped (depotBridge bridgeSlipped))
    (depot garden (depotHill hillGarden))))
(= (step-prob $q $edges) (foldl-atom $edges 1 $acc $e (* $acc (edge-prob $q $e))))
; A model maps a path to a probability: a step model multiplies its edges;
; the cache answers 9/10 for the whole bridge route and asks the step model
; otherwise.
(= (model-prob (step-model $q) $edges) (step-prob $q $edges))
(= (model-prob (cached $q) $edges)
   (if (== $edges (depotBridge bridgeGarden)) 9/10 (step-prob $q $edges)))
(= (append $xs $ys)
   (if (== $xs ()) $ys
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)) ($r (append $t $ys))) (cons-atom $h $r)){$h $t $r}))
(= (defect $model $first $second)
   (let* (($whole (append $first $second))
          ($x (model-prob $model $whole)) ($y (model-prob $model $first)) ($z (model-prob $model $second)))
     (abs-q (- $x (* $y $z)))))
; The largest defect over every composable pair of paths.
(= (max-defect $model)
   (let* (($ps (paths))
          ($ds (collapse (let* ((($a $b $f) (superpose $ps)) (($b2 $c $g) (superpose $ps)))
                           (if (== $b $b2) (defect $model $f $g) (empty))){$a $b $f $b2 $c $g})))
     (foldl-atom $ds 0 $acc $d (max-q $acc $d))))

; 5. A coherent step model is a functor on the route graph: defect 0 for every
;    composable pair, for rare slips and for never slipping
;    (pathCorrespondence_exact, coherent_defect_zero_metric_positive), though
;    their metrics are 29/400 and 3/40.
!(max-defect (step-model 1/100))
!(max-defect (step-model 0))
; 6. The cache, with the world's own step model, has defect 1/5 on the bridge
;    route (cachedCorrespondence_defect, cache_defect_step_exact), while its
;    step model is at metric 0 from the world (line 2), and defect 0 on the hill
;    route (cachedCorrespondence_hill_defect).
!(let $p (world-slip) (defect (cached $p) (depotBridge) (bridgeGarden)))
!(let $p (world-slip) (defect (cached $p) (depotHill) (hillGarden)))

; ---- goal-weighted averages ------------------------------------------------------------

; 7. Today's goal weights the hill route: the cache's average defect is 0
;    (today_average_zero).  Tomorrow's weights the bridge route: 1/5
;    (tomorrow_average).  Today's weighting does not charge tomorrow's goal, so
;    nothing transfers.
!(let* (($p (world-slip)) ($h (defect (cached $p) (depotHill) (hillGarden)))
        ($b (defect (cached $p) (depotBridge) (bridgeGarden))))
   ((+ (* 1 $h) (* 0 $b)) (+ (* 0 $h) (* 1 $b))))
; 8. The cached conclusion "the bridge route delivers with probability at
;    least 17/20" holds in the cache, fails by composing the step model, and
;    fails in the world, where the undiscounted value of "deliver over the
;    bridge" is 7/10 (cached_conclusion_fails).
!(let* (($p (world-slip)) ($phi (bridge-delivery))
        ($cache (model-prob (cached $p) (depotBridge bridgeGarden)))
        ($legs (* (model-prob (cached $p) (depotBridge)) (model-prob (cached $p) (bridgeGarden))))
        ($world (ev (delivery $p) 1 $phi depot)))
   ((<= 17/20 $cache) (< $legs 17/20) (< $world 17/20)))
