; Constraint planning: a total search that returns a plan or a checked
; refutation, beside a budgeted search that can only run out.
;
; Four packages with versions: web 1, 2, 3; db 1, 2; logger 1, 2, 3; auth 1, 2.
; (The logging package is called logger here: log is a host function, and a
; list headed by it is evaluated as a call.)
; Constraints, numbered from 0:
;   0  web at version 2 or later is requested
;   1  web at 2 or later needs db at 2
;   2  web at 3 or later needs logger at 2 or later
;   3  auth at 2 cannot be combined with db at 2
;   4  logger at 3 or later needs auth at 2
; The tightened problem adds
;   5  web at 2 or later needs auth at 2
; which, with 1 and 3, leaves no plan.
;
; An assignment is a list of (package version), the most recent first; a
; constraint is violated when its packages all have versions and those
; versions are not allowed.  Backtracking search checks the first violated
; constraint, then picks a pending package by a strategy and tries its
; versions in order:
;   first    the first pending package;
;   fewest   the pending package with the fewest versions that violate no
;            constraint when assigned, the first such on ties.
; It returns a plan, or a refutation certificate: a decision tree whose
; leaves (conflict i) name violated constraints and whose nodes
; (split package branches) have one branch per version, in order.  An
; independent checker replays a certificate: a leaf must name a constraint
; violated by the assignment reached, a node must split a package of the
; problem with exactly one branch per version.  An accepted certificate
; excludes every plan.
;
; A budgeted run executes a machine for at most k steps: a machine step either
; continues with a new state or stops with an answer.  Stopping is
; Established; running out is Incomplete, with the current state as its
; receipt.  Two machines: an enumeration of the 36 assignments of the
; problem, cycling, that stops at the first plan (candidate n is assignment
; n mod 36, read in mixed radix with web slowest and auth fastest); and a
; puzzle whose first witness n has (n * 2654435761 + 12345) mod 1000 < 4.
;
; Mirrors: solve_inl, solve_inr, check_sound, no_solution, solvable_iff,
; plan_is_solution, old_web_not_solution, tightened_unsolvable,
; outcome_decided, runFor_ne_refuted, runFor_budgetRefines,
; Search.run_incomplete, Search.established_iff, and the computed values of
; the version-planning examples (the two plans, the fewest-strategy
; certificate, the rejected certificates, the puzzle's budgets).
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

; ---- lists ------------------------------------------------------------------------

(= (in-list $x $xs)
   (if (== $xs ()) False
       (let $h (car-atom $xs)
         (if (== $h $x) True (let $t (cdr-atom $xs) (in-list $x $t)){$t})){$h}))
(= (append $xs $ys)
   (if (== $xs ()) $ys
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)) ($r (append $t $ys))) (cons-atom $h $r)){$h $t $r}))
(= (nth $xs $i)
   (if (== $xs ()) out-of-range
       (if (== $i 0) (car-atom $xs) (let* (($t (cdr-atom $xs)) ($j (- $i 1))) (nth $t $j)){$t $j})))

; ---- the problems -----------------------------------------------------------------

(= (packages) (web db logger auth))
(= (versions web) (1 2 3))
(= (versions db) (1 2))
(= (versions logger) (1 2 3))
(= (versions auth) (1 2))
(= (rules)
   ((at-least web 2) (needs web 2 db (equals 2)) (needs web 3 logger (no-less 2))
    (conflicts auth 2 db 2) (needs logger 3 auth (equals 2))))
(= (tightened-rules) (let $r (rules) (append $r ((needs web 2 auth (equals 2))))))

(= (scope (at-least $x $v)) ($x))
(= (scope (needs $x $v $y $ok)) ($x $y))
(= (scope (conflicts $x $v $y $w)) ($x $y))
(= (allows (at-least $x $v) ($a)) (<= $v $a))
(= (allows (needs $x $v $y $ok) ($a $b)) (or (< $a $v) (ok-holds $ok $b)))
(= (allows (conflicts $x $v $y $w) ($a $b)) (not (and (== $a $v) (== $b $w))))
(= (ok-holds (equals $n) $b) (== $b $n))
(= (ok-holds (no-less $n) $b) (<= $n $b))

; ---- assignments and violations -------------------------------------------------------

(= (version-of $a $x)
   (if (== $a ()) none
       (let* ((($y $d) (car-atom $a)) ($rest (cdr-atom $a)))
         (if (== $y $x) $d (version-of $rest $x))){$y $d $rest}))
(= (values $a $xs)
   (if (== $xs ()) ()
       (let* (($x (car-atom $xs)) ($d (version-of $a $x)))
         (if (== $d none) none
             (let* (($t (cdr-atom $xs)) ($ds (values $a $t)))
               (if (== $ds none) none (cons-atom $d $ds))){$t $ds})){$x $d}))
(= (violated $c $a)
   (let* (($s (scope $c)) ($ds (values $a $s)))
     (if (== $ds none) False (not (allows $c $ds)))))
(= (first-violation $cs $a $i)
   (if (== $cs ()) none
       (let* (($c (car-atom $cs)) ($t (cdr-atom $cs)) ($j (+ $i 1)))
         (if (violated $c $a) $i (first-violation $t $a $j))){$c $t $j}))

; ---- strategies ------------------------------------------------------------------------

(= (score $cs $a $x)
   (let $ds (versions $x)
     (foldl-atom $ds 0 $n $d
       (let* (($a2 (cons-atom ($x $d) $a)) ($i (first-violation $cs $a2 0)))
         (if (== $i none) (+ $n 1) $n)))))
(= (least-by $cs $a $x $xs)
   (if (== $xs ()) $x
       (let* (($y (car-atom $xs)) ($ys (cdr-atom $xs)) ($best (least-by $cs $a $y $ys)))
         (if (<= (score $cs $a $x) (score $cs $a $best)) $x $best)){$y $ys $best}))
(= (choose first $cs $a $pending) (car-atom $pending))
(= (choose fewest $cs $a $pending)
   (let* (($x (car-atom $pending)) ($xs (cdr-atom $pending))) (least-by $cs $a $x $xs)))

; ---- backtracking search ----------------------------------------------------------------

; (found plan) or (refutation tree).
(= (backtrack $cs $strat $a $pending)
   (let $i (first-violation $cs $a 0)
     (if (== $i none)
         (if (== $pending ()) (found $a)
             (let* (($x (choose $strat $cs $a $pending)) ($ds (versions $x))
                    ($rest (filter-atom $pending $y (not (== $y $x))){$y}))
               (try-versions $cs $strat $a $x $ds $rest ())){$x $ds $rest})
         (refutation (conflict $i)))))
(= (try-versions $cs $strat $a $x $ds $rest $trees)
   (if (== $ds ()) (refutation (split $x $trees))
       (let* (($d (car-atom $ds)) ($dt (cdr-atom $ds)) ($a2 (cons-atom ($x $d) $a))
              ($r (backtrack $cs $strat $a2 $rest)) ($tag (car-atom $r)))
         (if (== $tag found) $r
             (let* (($t (index-atom $r 1)) ($trees2 (append $trees ($t))))
               (try-versions $cs $strat $a $x $dt $rest $trees2)){$t $trees2})){$d $dt $a2 $r $tag}))
; The total search, as an outcome: a plan or a refutation, never Incomplete.
(= (solve $strat $cs)
   (let* (($ps (packages)) ($r (backtrack $cs $strat () $ps)) ($tag (car-atom $r)) ($v (index-atom $r 1)))
     (if (== $tag found) (Established $v) (Refuted $v))))

; ---- the checkers ------------------------------------------------------------------------

(= (check $cs $a (conflict $i))
   (let $c (nth $cs $i) (if (== $c out-of-range) False (violated $c $a))))
(= (check $cs $a (split $x $bs))
   (let* (($ps (packages)) ($ds (versions $x)))
     (and (in-list $x $ps) (check-branches $cs $a $x $ds $bs))))
(= (check-branches $cs $a $x $ds $bs)
   (if (== $ds ()) (== $bs ())
       (if (== $bs ()) False
           (let* (($d (car-atom $ds)) ($dt (cdr-atom $ds)) ($b (car-atom $bs)) ($bt (cdr-atom $bs))
                  ($a2 (cons-atom ($x $d) $a)))
             (and (check $cs $a2 $b) (check-branches $cs $a $x $dt $bt))){$d $dt $b $bt $a2})))
; A plan: every package has one of its versions, and every constraint holds.
(= (is-plan $cs $f)
   (let* (($ps (packages))
          ($typed (foldl-atom $ps True $ok $x
                    (let* (($d (version-of $f $x)) ($vs (versions $x))) (and $ok (in-list $d $vs)))))
          ($held (foldl-atom $cs True $ok $c
                   (let* (($s (scope $c)) ($ds (values $f $s)))
                     (and $ok (if (== $ds none) False (allows $c $ds)))))))
     (and $typed $held)))

; ---- budgeted runs --------------------------------------------------------------------------

(= (run-for $machine $k $s)
   (if (== $k 0) (Incomplete $s)
       (let $r (step-of $machine $s)
         (unify $r (continue $s2) (let $k2 (- $k 1) (run-for $machine $k2 $s2)){$k2}
           (unify $r (stop $b) (Established $b) (bad-step $r)))){$r}))
; Candidate n: the assignment numbered n mod 36, web slowest and auth fastest.
(= (candidate $n)
   (let* (($i (% $n 36)) ($iw (// $i 12)) ($r (% $i 12)) ($id (// $r 6)) ($r2 (% $r 6))
          ($il (// $r2 2)) ($ia (% $r2 2))
          ($ws (versions web)) ($ds (versions db)) ($ls (versions logger)) ($as (versions auth))
          ($w (nth $ws $iw)) ($d (nth $ds $id)) ($l (nth $ls $il)) ($u (nth $as $ia)))
     ((auth $u) (logger $l) (db $d) (web $w))))
(= (step-of (plan-search $cs) $n)
   (let $f (candidate $n)
     (if (is-plan $cs $f) (stop (witness $n $f)) (continue (+ $n 1)))))
(= (step-of puzzle $n)
   (if (< (% (+ (* $n 2654435761) 12345) 1000) 4) (stop (witness $n)) (continue (+ $n 1))))
(= (status $v) (car-atom $v))

; ---- plans ---------------------------------------------------------------------------------

; 1. The plans found by the two strategies (solve_inl).
!(let $cs (rules) (solve first $cs))
!(let $cs (rules) (solve fewest $cs))
; 2. The first is a plan; with web at version 1 it is not, since the request
;    is violated (plan_is_solution, old_web_not_solution).
!(let $cs (rules)
   ((is-plan $cs ((auth 1) (logger 1) (db 2) (web 2))) (is-plan $cs ((auth 1) (logger 1) (db 2) (web 1)))))

; ---- refutations -----------------------------------------------------------------------------

; 3. The tightened problem has no plan: the first strategy returns a
;    refutation, and the checker accepts it (solve_inr, tightened_unsolvable).
!(let* (($cs (tightened-rules)) ((Refuted $t) (solve first $cs))) (Refuted (check $cs () $t)))
; 4. The fewest strategy returns the certificate: web 1 violates the request
;    (0), db 1 violates rule 1, and with db 2 either auth version breaks rule
;    5 or rule 3; the checker accepts it (check_sound, no_solution).
!(let $cs (tightened-rules) (solve fewest $cs))
!(let* (($cs (tightened-rules)) ((Refuted $t) (solve fewest $cs))) (check $cs () $t))
; 5. Bad certificates are rejected: no constraint is violated before anything
;    is assigned; a split needs one branch per version; and the satisfiable
;    problem accepts neither refutation of the tightened one (no_solution with
;    plan_is_solution).
!(let* (($tc (tightened-rules)) ($cs (rules))
        ((Refuted $t1) (solve first $tc)) ((Refuted $t2) (solve fewest $tc)))
   ((check $tc () (conflict 0)) (check $tc () (split web ((conflict 0))))
    (check $cs () $t1) (check $cs () $t2)))
; 6. The total search is decided: a plan or a checked refutation, never
;    Incomplete (outcome_decided).
!(let* (($cs (rules)) ($tc (tightened-rules)))
   ((status (solve first $cs)) (status (solve fewest $cs))
    (status (solve first $tc)) (status (solve fewest $tc))))

; ---- budgets ---------------------------------------------------------------------------------

; 7. Enumerating assignments of the tightened problem runs out at every
;    budget, up to all 36 assignments, and running out is Incomplete with the
;    budget as its receipt, never Refuted, although no plan exists
;    (runFor_ne_refuted, Search.run_incomplete, tightened_unsolvable).
!(let $tc (tightened-rules) (map-atom (0 12 36) $k (run-for (plan-search $tc) $k 0)))
; 8. For the satisfiable problem, 36 steps reach a plan: Established, and the
;    witness is a plan (Search.established_iff with plan_is_solution).
!(let* (($cs (rules)) ((Established (witness $n $f)) (run-for (plan-search $cs) 36 0)))
   (Established (is-plan $cs $f)))
; 9. Raising the budget refines the outcome: the puzzle is Incomplete at
;    budget 100 and Established at 2000 (runFor_budgetRefines).
!(map-atom (100 2000) $k (let $v (run-for puzzle $k 0) (status $v)))
