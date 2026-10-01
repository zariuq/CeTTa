; One program, many interpretations.  The road map of road_revision, with
; its roads read in one direction: a->b, b->d, a->c, c->d.  The path program
; finds routes of two roads from s to t: it joins the road relation with
; itself and keeps the endpoints.  It is written once, over a semiring given
; by its operations, and run in four:
;   prov   polynomials over the roads, which record the roads a route used
;   reach  booleans with or and and: is there a route
;   cost   the minimum and addition, with inf for no route: the cheapest one
;   count  numbers with + and *: how many routes
; A polynomial is written as a list of monomials, a monomial as a list of
; roads: ((ab bd) (ac cd)) is ab*bd + ac*cd.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

(= (ends ab) (a b))
(= (ends bd) (b d))
(= (ends ac) (a c))
(= (ends cd) (c d))
(= (road-cost ab) 1)
(= (road-cost bd) 5)
(= (road-cost ac) 2)
(= (road-cost cd) 2)

; ---- the program, over any semiring $K --------------------------------------

; The road relation: the road from s to t carries its tag.
(= (edge $K $s $t)
   (let $z (zero $K)
     (foldl-atom (ab bd ac cd) $z $acc $r
       (let ($x $y) (ends $r)
         (plus $K $acc (if (and (== $x $s) (== $y $t)) (tag $K $r) (zero $K)))))))
; Routes of two roads: the sum over middle towns of edge(s,m) * edge(m,t).
(= (two-roads $K $s $t)
   (let $z (zero $K)
     (foldl-atom (a b c d) $z $acc $m
       (plus $K $acc (times $K (edge $K $s $m) (edge $K $m $t))))))

; ---- four semirings -----------------------------------------------------------

(= (concat $xs $ys)
   (if (== $xs ()) $ys
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)) ($r (concat $t $ys)))
         (cons-atom $h $r))))

(= (zero prov) ())
(= (one prov) (()))
(= (tag prov $r) (($r)))
(= (plus prov $p $q) (let* (($x $p) ($y $q)) (concat $x $y)))
(= (times prov $p $q)
   (let* (($x $p) ($y $q))
     (collapse (let* (($m1 (superpose $x)) ($m2 (superpose $y))) (concat $m1 $m2)))))

(= (zero reach) False)
(= (one reach) True)
(= (tag reach $r) True)
(= (plus reach $x $y) (or $x $y))
(= (times reach $x $y) (and $x $y))

(= (zero cost) inf)
(= (one cost) 0)
(= (tag cost $r) (road-cost $r))
(= (plus cost $x $y)
   (if (== $x inf) $y (if (== $y inf) $x (if (< $x $y) $x $y))))
(= (times cost $x $y)
   (if (== $x inf) inf (if (== $y inf) inf (+ $x $y))))

(= (zero count) 0)
(= (one count) 1)
(= (tag count $r) 1)
(= (plus count $x $y) (+ $x $y))
(= (times count $x $y) (* $x $y))

; Evaluating a polynomial in a semiring: sum its monomials, multiply the
; tags of the roads in each.
(= (evaluate $K $p)
   (let* (($z (zero $K)) ($o (one $K)))
     (foldl-atom $p $z $acc $m
       (plus $K $acc (foldl-atom $m $o $pr $r (times $K $pr (tag $K $r)))))))

; Polynomials are equal when they have the same monomials, with multiplicity.
(= (remove-one $x $ys)
   (if (== $ys ()) no-match
       (let* (($y (car-atom $ys)) ($rest (cdr-atom $ys)))
         (if (bag-eq $x $y) $rest
             (let $r (remove-one $x $rest)
               (if (== $r no-match) no-match (cons-atom $y $r)))))))
(= (bag-eq $xs $ys)
   (if (== (get-metatype $xs) Expression)
       (if (== $xs ()) (== $ys ())
           (let* (($x (car-atom $xs)) ($rest (cdr-atom $xs)) ($left (remove-one $x $ys)))
             (if (== $left no-match) False (bag-eq $rest $left))))
       (== $xs $ys)))

(= (provenance $s $t) (two-roads prov $s $t))

; 1. The provenance of the routes from a to d is ab*bd + ac*cd
;    (provenance_a_d).
!(let $p (provenance a d) (bag-eq $p ((ab bd) (ac cd))))
; 2. Its three images: d is reachable (reach_a_d), the cheapest route costs
;    4, through c (cost_a_d), and there are two routes (count_a_d).
!(let $p (provenance a d) (evaluate reach $p))
!(let $p (provenance a d) (evaluate cost $p))
!(let $p (provenance a d) (evaluate count $p))
; 3. Running the program in a semiring is evaluating its provenance there
;    (twoRoads_eq_evaluate).
!(let* (($p (provenance a d))
        ($reach (two-roads reach a d)) ($cost (two-roads cost a d)) ($count (two-roads count a d)))
   (and (== $reach (evaluate reach $p))
        (and (== $cost (evaluate cost $p)) (== $count (evaluate count $p)))))
; 4. Reachability can be read off the count and off the cost
;    (reach_factors_count, reach_factors_cost), checked on some polynomials,
;    among them the zero polynomial ().
(= (samples) (superpose (((ab)) ((ab) (bd)) ((ab) (ab)) ((bd)) ((ab bd) (ac cd)) ())))
(= (reach-from-count $p) (let $n (evaluate count $p) (< 0 $n)))
(= (reach-from-cost $p) (let $c (evaluate cost $p) (not (== $c inf))))
!(let $bad (collapse (let $p (samples)
                       (if (and (== (evaluate reach $p) (reach-from-count $p))
                                (== (evaluate reach $p) (reach-from-cost $p)))
                           (empty) $p)))
   (== $bad ()))
; 5. Nothing else descends: two polynomials with one image and different
;    values of another (count_not_factors_reach, cost_not_factors_reach,
;    count_not_factors_cost, cost_not_factors_count).
(= (same-but-different $view $consumer $p $q)
   (and (== (evaluate $view $p) (evaluate $view $q))
        (not (== (evaluate $consumer $p) (evaluate $consumer $q)))))
!(same-but-different reach count ((ab)) ((ab) (bd)))
!(same-but-different reach cost ((ab)) ((bd)))
!(same-but-different cost count ((ab)) ((ab) (ab)))
!(same-but-different count cost ((ab)) ((bd)))
