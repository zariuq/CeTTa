; PLN induction: ravens are black, black things are not ravens.
;
; Observed individuals are facts in a space.  Evidence for an inheritance
; link A -> B is a pair of counts (ev n+ n-): an individual with A and B
; adds (ev 1 0), one with A and not B adds (ev 0 1), and one without A adds
; nothing.  Induction folds revision (coordinatewise addition) over the
; observations.  Strength and confidence are read off the counts:
;
;   strength   (ev p n) = p / (p + n)            (0 when there is no evidence)
;   confidence (ev p n) = (p + n) / (p + n + k)  with k = 1
;
; A link is a pair of lambda predicates, run as ordinary code on each
; observation.  Every rational is exact: (ratio a b) is a/b in lowest terms.
;
; A line (Lean <theorem> <runtime value> <check>) prints what this run
; computes next to the name of the Lean theorem that states the same fact
; about the same dataset; <check> is this run's own test of the stated
; relation.  The theorems are in the PLN raven curriculum
; (Mettapedia.Examples.PLN.RavenInductionCurriculum), the rule family
; RavenAsymmetricInduction and its Bayes-inversion bridge
; (RavenInductionBridge).  (LeanDef <definition> ...) lines do the same for
; definitions.

; ---------------------------------------------------------------------------
; Exact rationals.  (q a b) is a/b in lowest terms, as (frac n d) with d > 0.

(= (gcd $a $b) (if (== $b 0) $a (let $r (% $a $b) (gcd $b $r))))
(= (q $a $b)
   (let $g (gcd $a $b)
     (let $n (// $a $g)
       (let $d (// $b $g)
         (frac $n $d)))))
(= (q-le (frac $a $b) (frac $c $d)) (<= (* $a $d) (* $c $b)))
(= (q-lt (frac $a $b) (frac $c $d)) (< (* $a $d) (* $c $b)))
(= (q-eq (frac $a $b) (frac $c $d)) (== (* $a $d) (* $c $b)))
(= (q-mul (frac $a $b) (frac $c $d))
   (let $n (* $a $c) (let $m (* $b $d) (q $n $m))))
(= (q-div (frac $a $b) (frac $c $d))
   (let $n (* $a $d) (let $m (* $b $c) (q $n $m))))
(= (shown (frac $a $b)) (if (== $b 1) $a (ratio $a $b)))

; ---------------------------------------------------------------------------
; Evidence counts are primal.

(= (revision (ev $p1 $n1) (ev $p2 $n2))
   (let $p (+ $p1 $p2) (let $n (+ $n1 $n2) (ev $p $n))))
(= (total (ev $p $n)) (+ $p $n))
(= (strength (ev $p $n))
   (let $t (+ $p $n) (if (== $t 0) (frac 0 1) (q $p $t))))
(= (confidence $k (ev $p $n))
   (let $t (+ $p $n) (let $u (+ $t $k) (q $t $u))))
(= (lens) 1)

; Guarded Bayes inversion, as in the bridge: P(A|B) = P(B|A) P(A) / P(B),
; admissible when every input is a probability, P(B) > 0, and the joint
; P(B|A) P(A) does not exceed P(B).
(= (bayes-inversion $s-ab $p-a $p-b) (q-div (q-mul $s-ab $p-a) $p-b))
(= (admissible $s-ab $p-a $p-b)
   (and (q-le (frac 0 1) $s-ab)
     (and (q-le $s-ab (frac 1 1))
       (and (q-le (frac 0 1) $p-a)
         (and (q-le $p-a (frac 1 1))
           (and (q-lt (frac 0 1) $p-b)
             (and (q-le $p-b (frac 1 1))
               (q-le (q-mul $s-ab $p-a) $p-b))))))))

; ---------------------------------------------------------------------------
; The world.  An observed individual carries the properties it was seen with.

!(bind! &world (new-space))

(= (member $x $props)
   (if (== $props ()) False
       (let ($h $t) (decons-atom $props)
         (if (== $h $x) True (member $x $t)))))
(= (raven (seen $id $props)) (member Raven $props))
(= (black (seen $id $props)) (member Black $props))

(= (observe-range $tag $from $to $props)
   (if (> $from $to) done
       (let $id ($tag $from)
         (let $u (add-atom &world (observed $id $props))
           (let $next (+ $from 1) (observe-range $tag $next $to $props))))))
(= (observe $tag $from $to $props)
   (let $u (observe-range $tag $from $to $props)
     (observed (from ($tag $from)) (to ($tag $to)) $props)))
(= (observations)
   (collapse (match &world (observed $id $props) (seen $id $props))))
(= (count-kind $props)
   (let $ids (collapse (match &world (observed $id $props) $id)) (size-atom $ids)))

; The four directed links, each a pair of lambda predicates.
(= (link raven→black) (→ (lam x (raven x)) (lam x (black x))))
(= (link black→raven) (→ (lam x (black x)) (lam x (raven x))))
(= (link non-black→non-raven) (→ (lam x (not (black x))) (lam x (not (raven x)))))
(= (link non-raven→non-black) (→ (lam x (not (raven x))) (lam x (not (black x)))))

; One observation's evidence for a link: directional, only members of A count.
(= (contribution (→ $a $b) $o)
   (if ($a $o) (if ($b $o) (ev 1 0) (ev 0 1)) (ev 0 0)))

; Induction is iterated revision, one observation at a time, from no
; evidence: a left fold, as in the Lean aggregateEvidence.
(= (aggregate-over $name $obs)
   (let $l (link $name)
     (foldl-atom $obs (ev 0 0) $acc $o (revision $acc (contribution $l $o)))))
(= (aggregate $name) (aggregate-over $name (observations)))

(= (reversed $xs) (foldl-atom $xs () $acc $x (cons-atom $x $acc)))

(= (summary $name)
   (let $e (aggregate $name)
     (let $s (strength $e)
       (let $c (confidence (lens) $e)
         (let $ss (shown $s)
           (let $cs (shown $c)
             ($name $e (strength $ss) (confidence $cs))))))))

!(link raven→black)
!(link black→raven)

; ---------------------------------------------------------------------------
; The per-observation rule on the two observation kinds of the Lean model
; (a black raven, a black non-raven) is exactly the Lean contribution table.

!(let $br (seen sample (Raven Black))
   (let $bn (seen sample (Black))
     (let $f1 (contribution (link raven→black) $br)
       (let $f2 (contribution (link raven→black) $bn)
         (LeanDef ravenBlackContribution (blackRaven $f1) (blackNonRaven $f2))))))
!(let $br (seen sample (Raven Black))
   (let $bn (seen sample (Black))
     (let $i1 (contribution (link black→raven) $br)
       (let $i2 (contribution (link black→raven) $bn)
         (LeanDef blackRavenContribution (blackRaven $i1) (blackNonRaven $i2))))))

; ---------------------------------------------------------------------------
; Induction one raven at a time.

!(observe bird 1 4 (Raven Black))
!(let $f4 (aggregate raven→black)
   (let $i4 (aggregate black→raven)
     (let $u (observe bird 5 5 (Raven Black))
       (let $f5 (aggregate raven→black)
         (let $i5 (aggregate black→raven)
           (let $rf (revision $f4 (ev 1 0))
             (let $ri (revision $i4 (ev 1 0))
               (Lean (ravenBlackEvidence_succ blackRavenEvidence_succ_raven)
                 (after-fourth-raven $f4 $i4) (after-fifth-raven $f5 $i5)
                 (and (== $f5 $rf) (== $i5 $ri))))))))))

; ---------------------------------------------------------------------------
; The curriculum dataset: 5 black ravens, then 95 black non-ravens.

!(observe bird 6 100 (Black))

!(let $f (aggregate raven→black)
   (Lean five_ravens_forward_aggregate $f (== $f (ev 5 0))))
!(let $f (aggregate raven→black)
   (let $s (strength $f)
     (let $ss (shown $s)
       (Lean five_ravens_forward_strength $ss (q-eq $s (frac 1 1))))))
!(let $i (aggregate black→raven)
   (Lean five_ravens_inverse_aggregate $i (== $i (ev 5 95))))
!(let $i (aggregate black→raven)
   (let $s (strength $i)
     (let $ss (shown $s)
       (Lean five_ravens_inverse_strength $ss (q-eq $s (frac 1 20))))))
!(let $fs (strength (aggregate raven→black))
   (let $is (strength (aggregate black→raven))
     (let $a (shown $is)
       (let $b (shown $fs)
         (let $same (q-eq $is $fs)
           (Lean inverse_not_forward_strength (≠ $a $b) (not $same)))))))

; The aggregates depend only on the two counts, read here straight from the
; facts by observation kind, and not on the order of the observations.
!(let $r (count-kind (Raven Black))
   (let $m (count-kind (Black))
     (let $f (aggregate raven→black)
       (let $i (aggregate black→raven)
         (Lean (aggregate_ravenBlack_eq_count aggregate_blackRaven_eq_counts)
           (counts (black-ravens $r) (black-nonravens $m))
           (and (== $f (ev $r 0)) (== $i (ev $r $m))))))))
!(let $obs (observations)
   (let $rev (reversed $obs)
     (let $f1 (aggregate-over raven→black $obs)
       (let $f2 (aggregate-over raven→black $rev)
         (let $i1 (aggregate-over black→raven $obs)
           (let $i2 (aggregate-over black→raven $rev)
             (Lean (aggregate_ravenBlack_perm aggregate_blackRaven_perm)
               (reversed-order $f2 $i2)
               (and (== $f1 $f2) (== $i1 $i2)))))))))
!(let $r (count-kind (Raven Black))
   (let $m (count-kind (Black))
     (let $obs (observations)
       (let $n (size-atom $obs)
         (Lean blackObservationCount_eq_length (+ $r $m) (== (+ $r $m) $n))))))
!(let $f (aggregate raven→black)
   (let $i (aggregate black→raven)
     (let $tf (total $f)
       (let $ti (total $i)
         (Lean (total_ravenBlack total_blackRaven) (totals $tf $ti)
           (and (== $tf 5) (== $ti 100)))))))

; The inverse strength is Bayes inversion of the forward strength through the
; observed raven base rate R/(R+M) among black things, where P(black) = 1.
!(let $r (count-kind (Raven Black))
   (let $m (count-kind (Black))
     (let $base (q $r (+ $r $m))
       (let $fs (strength (aggregate raven→black))
         (let $is (strength (aggregate black→raven))
           (let $bayes (bayes-inversion $fs $base (frac 1 1))
             (let $v1 (shown $fs)
               (let $v2 (shown $is)
                 (let $v3 (shown $base)
                   (let $v4 (shown $bayes)
                     (Lean ravenInduction_bayesInversion_values_canary
                       ($v1 $v2 $v3 $v4)
                       (and (q-eq $fs (frac 1 1))
                         (and (q-eq $is (frac 1 20))
                           (and (q-eq $base (frac 1 20))
                             (q-eq $bayes (frac 1 20))))))))))))))))
!(let $r (count-kind (Raven Black))
   (let $m (count-kind (Black))
     (let $base (q $r (+ $r $m))
       (let $fs (strength (aggregate raven→black))
         (let $bayes (bayes-inversion $fs $base (frac 1 1))
           (let $ok (admissible $fs $base (frac 1 1))
             (let $unit (and (q-le (frac 0 1) $bayes) (q-le $bayes (frac 1 1)))
               (let $v (shown $bayes)
                 (Lean ravenInduction_bayesInversion_guard_canary
                   (admissible $ok) (in-unit-interval $v)
                   (and $ok $unit))))))))))

; ---------------------------------------------------------------------------
; The white shoe.  The Lean model has only the two observation kinds above
; and states nothing about non-black observations, so the lines below cite no
; theorem: they apply the same directional rule to a third kind.  A white
; shoe is neither black nor a raven.  It is evidence for non-black ->
; non-raven, which PLN keeps directionally separate from raven -> black: a
; link and its contrapositive rest on different observations, so the forward
; link's counts do not move.

!(white-shoe before-white-shoes
   (summary raven→black) (summary non-black→non-raven)
   (summary black→raven) (summary non-raven→non-black))
!(observe shoe 1 10 (White Shoe))
!(white-shoe after-10-white-shoes
   (summary raven→black) (summary non-black→non-raven)
   (summary black→raven) (summary non-raven→non-black))

; ---------------------------------------------------------------------------
; More black non-ravens.  The inverse estimate grows more confident and weaker
; at once; the forward link never moves.  Each row is also recorded, to check
; the general monotonicity and Bayes-inversion theorems over every pair.

!(bind! &rows (new-space))
(= (inverse-row)
   (let $r (count-kind (Raven Black))
   (let $m (count-kind (Black))
     (let $i (aggregate black→raven)
       (let $f (aggregate raven→black)
         (let $u (add-atom &rows (row $r $m $i $f))
           (let $s (shown (strength $i))
             (let $c (shown (confidence (lens) $i))
               (let $fs (shown (strength $f))
                 (let $below (q-lt (strength $i) (strength $f))
                   (Lean (strength_blackRaven_of_observations
                          raven_black_strictly_stronger_of_observations)
                     (black→raven $i (strength $s) (confidence $c))
                     (raven→black $f (strength $fs))
                     (below $s $fs) $below)))))))))))
!(inverse-row)
!(let $e95 (aggregate black→raven)
   (let $u (observe bird 101 101 (Black))
     (let $e96 (aggregate black→raven)
       (let $step (revision $e95 (ev 0 1))
         (Lean blackRavenEvidence_succ_nonraven $e96 (== $e96 $step))))))
!(observe bird 102 200 (Black))
!(inverse-row)
!(observe bird 201 500 (Black))
!(inverse-row)
!(observe bird 501 1000 (Black))
!(inverse-row)

(= (row-pairs)
   (collapse
     (match &rows (row $r1 $m1 $i1 $f1)
       (match &rows (row $r2 $m2 $i2 $f2)
         (if (< $m1 $m2) (pair $i1 $i2) (empty))))))
(= (all-true $flags) (foldl-atom $flags True $a $b (and $a $b)))
!(let $pairs (row-pairs)
   (let $n (size-atom $pairs)
     (let $flags
          (collapse
            (let (pair $e1 $e2) (superpose $pairs)
              (q-le (strength $e2) (strength $e1))))
       (let $ok (all-true $flags)
         (Lean strength_blackRaven_antitone (row-pairs $n) $ok)))))
!(let $pairs (row-pairs)
   (let $n (size-atom $pairs)
     (let $flags
          (collapse
            (let (pair $e1 $e2) (superpose $pairs)
              (q-le (confidence (lens) $e1) (confidence (lens) $e2))))
       (let $ok (all-true $flags)
         (Lean confidence_blackRaven_monotone (row-pairs $n) $ok)))))
!(let $rows (collapse (match &rows (row $r $m $i $f) (row $r $m $i $f)))
   (let $n (size-atom $rows)
     (let $flags
          (collapse
            (let (row $r $m $i $f) (superpose $rows)
              (let $base (q $r (+ $r $m))
                (let $bayes (bayes-inversion (strength $f) $base (frac 1 1))
                  (and (q-eq (strength $i) $base)
                    (q-eq (strength $i) $bayes))))))
       (let $ok (all-true $flags)
         (Lean (blackRavenStrength_toReal_eq_baseRate
                blackRavenStrength_toReal_eq_bayesInversion)
           (rows $n) $ok)))))

; The two curriculum comparisons, 95 against 995 black non-ravens.
!(let $c95 (confidence (lens) (ev 5 95))
   (let $e (aggregate black→raven)
     (let $now (confidence (lens) $e)
       (let $a (shown $c95)
         (let $b (shown $now)
           (Lean inverse_confidence_grows_with_more_black_nonravens
             (≤ $a $b) (and (== $e (ev 5 995)) (q-le $c95 $now))))))))
!(let $s95 (strength (ev 5 95))
   (let $e (aggregate black→raven)
     (let $now (strength $e)
       (let $a (shown $now)
         (let $b (shown $s95)
           (Lean inverse_strength_drops_with_more_black_nonravens
             (≤ $a $b) (and (== $e (ev 5 995)) (q-le $now $s95))))))))

; strength_blackRaven_tendsto_zero says the inverse strength tends to 0 as the
; black non-ravens grow without bound; the rows above are consistent with it,
; but no finite run checks a limit.
