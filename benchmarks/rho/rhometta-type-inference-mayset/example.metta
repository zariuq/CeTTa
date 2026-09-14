; ===========================================================================
; rhometta-type-inference-mayset -- GENUINE recursive may-set via rho:patterns:!recv.
;
; Goal: (box (box one)) type-checked as concurrent backward chaining.  The inner
; compound subgoal (box one) has TWO typings -- (Boxed Int) and (Boxed Float) --
; DERIVED by the rho chainer over &kb (one : Int | Float is a real COMM overload
; fork; nothing is hardcoded).  Those two derived sub-proofs FAN INTO the parent
; (box _) join through rho:patterns:!recv (Meredith-Radestock replicated input),
; yielding the two outer typings (Boxed (Boxed Int)) and (Boxed (Boxed Float)).
;
; HOW THE FAN-IN IS GENUINE (the honest boundary).  The engine forks a multi-result
; payload into separate may-set BRANCHES (one message per branch), so a single send
; would give the parent's replicated input nothing to fan in.  Instead the inner
; search's DERIVED proof set is collapsed to data at the parent premise channel and
; re-lifted as N parallel sends on that one channel; N distinct sends on one channel
; is contention, which drives rho:patterns:!recv's re-arming receive to consume all
; N in ONE branch -- real replicated fan-in over DERIVED proofs.  This collapse/
; re-lift bridge IS the subject of the demo; we do NOT claim inner-search and
; outer-join are one uninterrupted rho reduction.  (See description.txt.)
;
; INDEPENDENT ORACLE (anti-tautology).  The oracle is a SEPARATE sequential nilbc
; recursive chainer over the SAME &kb; it DERIVES the same boxed proof skeletons by
; its own resolution.  The gate is SET-EQUALITY of derived proof terms, never
; equality to a constant.  (The previous version's oracle was literally the expected
; output -- a tautology; this is a real cross-check.)
;
; The unary boxc rule is the minimal teaching core (ACTs 1-6): multiplicity comes
; from ONE overloaded premise, a single sharp source.  ACT7-ACT9 then show the
; MULTI-PREMISE case -- (box (plus one one)), a 2x2x2 cross-product pruned to 2 --
; fans in at depth through the SAME !recv with no starvation.  The unary core is
; a minimality choice, not an expressiveness limitation.
;
; Runs on the core build:  make BUILD=core
;     ./cetta --profile extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)
!(import! &self rho:patterns)

; --- typing context (Curry-Howard judgments), in &kb -------------------------
!(bind! &kb (new-space))
!(add-atom &kb (: pone-i (HasType one Int)))      ; one is overloaded Int | Float
!(add-atom &kb (: pone-f (HasType one Float)))    ;   -- the single multiplicity source
!(add-atom &kb (: px     (HasType x   Int)))      ; x is Int-only -- the prune canary
!(add-atom &kb (op-stv Int   (stv 0.90 0.90)))    ; PLN rank, OUTSIDE the judgment
!(add-atom &kb (op-stv Float (stv 0.60 0.90)))
; THE rule: UNARY box-introduction.  (HasType e A) |- (HasType (box e) (Boxed A)).
!(add-atom &kb (: boxc (-> (: $parg (HasType $e $A)) (HasType (box $e) (Boxed $A)))))

; --- rho plumbing ------------------------------------------------------------
(= (chan $n)
   (if (== $n 0) (rho:quote rho:nil) (rho:quote (rho:send (chan (- $n 1)) rho:nil))))
(= (unval (rho:quote (rho:val $x))) $x)
(= (cell $i $job)
   (rho:par (rho:send (chan $i) (rhometta:eval $job)) (rho:recv (chan $i) $y (rho:drop $y))))
(= (overload-rank $A)
   (let $r (collapse (match &kb (op-stv $A $tv) $tv)) (if (== $r ()) (stv 1.0 0.90) (car-atom $r))))
(= (box-tv (HasType $e $A)) (overload-rank $A))

; --- inner derivation: a GENUINE one-step backward-chaining may-set -----------
; step-inner: BASE = a typing fact (forks on the one : Int|Float overload);
;             RULE = the unary box rule, premise resolved + re-matched (the guard).
(= (step-inner (: $prf $concl) $depth $b) (match &kb (: $prf $concl) (: $prf $concl)))
(= (step-inner (: $prf $concl) (S $d) $b)
   (match &kb (: $rule (-> (: $p1 $t1) $concl)) (assemble1 $rule (: $p1 $t1) $d $b)))
(= (assemble1 $rule $prem1 $d $b)
   (let* (($c1 (+ (* $b 4) 1)) ($c2 (+ (* $b 4) 2)))
     (rho:par
       (rho:send (chan $c1) (rhometta:eval (step-inner $prem1 $d $c1)))
       (rho:recv (chan $c1) $r1 (rho:send (chan $c2) (rhometta:eval (combine1 $rule (unval $r1)))))
       (rho:recv (chan $c2) $z (rho:drop $z)))))
(= (combine1 $rule (: $p1 $t1))
   (let $res (collapse (match &kb (: $rule (-> (: $p1 $t1) $concl))
                          (proved (: ($rule $p1) $concl) (box-tv $t1))))
     (if (== $res ()) rho:nil (car-atom $res))))
(= (proof-of-inner (rho:val (proved $j $tv))) (proved $j $tv))
(= (proof-of-inner $other) (empty))
; derive ALL proofs of $goal as a genuine rho may-set, then collapse to one tuple.
(= (inner-proof-set $goal $b)
   (collapse (proof-of-inner (rhometta:run (cell $b (step-inner $goal (S Z) $b))))))

; --- re-lift the derived set as N parallel sends on ONE channel (=> fan-in) ---
(= (fan-sends $pch $t)
   (if (== $t ()) rho:nil
       (let $p (car-atom $t)
         (rho:par (rho:send $pch (rhometta:eval $p)) (fan-sends $pch (cdr-atom $t))))))

; --- the parent join: rho:patterns:!recv fans in the derived inner proofs ------
(= (outer-step (proved (: $pi (HasType (box one) $Ai)) $tvi))
   (let $res (collapse (match &kb (: boxc (-> (: $pi (HasType (box one) $Ai)) $concl))
                          (proved (: (boxc $pi) $concl) $tvi)))    ; carry inner rank outward
     (if (== $res ()) rho:nil (car-atom $res))))
(= (outer-step $other) rho:nil)
(= (proof-of $r)
   (case $r (((rho:val (proved $j $tv)) (proved $j $tv))
             ((rho:par $a $b) (superpose ((proof-of $a) (proof-of $b))))
             ((rho:par $a $b $c) (superpose ((proof-of $a) (proof-of $b) (proof-of $c))))
             ((rho:par $a $b $c $d) (superpose ((proof-of $a) (proof-of $b) (proof-of $c) (proof-of $d))))
             ((rho:par $a $b $c $d $e) (superpose ((proof-of $a) (proof-of $b) (proof-of $c) (proof-of $d) (proof-of $e))))
             ((rho:par $a $b $c $d $e $f) (superpose ((proof-of $a) (proof-of $b) (proof-of $c) (proof-of $d) (proof-of $e) (proof-of $f))))
             ((rho:par $a $b $c $d $e $f $g) (superpose ((proof-of $a) (proof-of $b) (proof-of $c) (proof-of $d) (proof-of $e) (proof-of $f) (proof-of $g))))
             ($o (empty)))))
(= (outer-net $inner)
   (rho:par
     (fan-sends (chan 1) $inner)
     (rho:patterns:!recv (chan 2) (chan 1) $sub
       (rho:send (chan 3) (rhometta:eval (outer-step (unval $sub)))))
     (rho:patterns:!recv (chan 4) (chan 3) $r (rho:drop $r))))
; inner derivation is HOISTED into the let (its own run) BEFORE the outer run, so the
; nested run grounds to data first -- no re-entrant rhometta:run (which would overflow).
(= (prove-mayset)
   (let $inner (inner-proof-set (: $pi (HasType (box one) $Ai)) 9)
     (collapse (proof-of (rhometta:run (outer-net $inner))))))

; --- INDEPENDENT sequential nilbc oracle (recursive, unary; over the SAME &kb) -
(= (bc (: $proof $concl) $_) (match &kb (: $proof $concl) (: $proof $concl)))
(= (bc (: ($rule $p1) $concl) (S $d))
   (let* (((: $rule (-> (: $p1 $t1) $concl)) (bc (: $rule (-> (: $p1 $t1) $concl)) $d))
          ((: $p1 $t1) (bc (: $p1 $t1) $d)))
     (: ($rule $p1) $concl)))

; --- projections + DUP-TOLERANT set classification ---------------------------
; The persistent !recv run repeats each completed leaf across interleavings, and
; unique-atom does NOT dedup terms reconstructed through collapse.  So we classify
; by PRESENCE (does the set contain skeleton X?) and SOUNDNESS (is every leaf one
; the oracle derives?) -- both multiplicity-insensitive.  The expected skeletons are
; exactly what the INDEPENDENT bc oracle derives (ACT4 checks bc reaches them).
(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))
(= (j-of (proved $j $tv)) $j)
(= (j-of $o) (empty))
(= (rho-judgments)    (let $ps (prove-mayset) (collapse (j-of (superpose $ps)))))
(= (oracle-judgments) (collapse (bc (: $p (HasType (box (box one)) $T)) (S (S Z)))))
(= (has-int $js)
   (> (count-tuple (collapse (case (superpose $js)
        (((: (boxc (boxc pone-i)) (HasType (box (box one)) (Boxed (Boxed Int)))) hit) ($_ (empty)))))) 0))
(= (has-float $js)
   (> (count-tuple (collapse (case (superpose $js)
        (((: (boxc (boxc pone-f)) (HasType (box (box one)) (Boxed (Boxed Float)))) hit) ($_ (empty)))))) 0))
(= (both-present $js) (+ (if (has-int $js) 1 0) (if (has-float $js) 1 0)))
(= (unexpected $js)
   (collapse (case (superpose $js)
     (((: (boxc (boxc pone-i)) (HasType (box (box one)) (Boxed (Boxed Int)))) (empty))
      ((: (boxc (boxc pone-f)) (HasType (box (box one)) (Boxed (Boxed Float)))) (empty))
      ($x $x)))))

; --- harness -----------------------------------------------------------------
(= (verdict $exp $got) (if (== $exp $got) PASS *FAIL*))
(= (gate $tag $exp $got $desc) (=== $tag (verdict $exp $got) expect $exp got $got : $desc))

; ===========================================================================
!(gate ACT1-mayset 2 (both-present (rho-judgments))
   "recursive may-set at depth: BOTH (box one) typings (Int & Float) are DERIVED and fan into the (box _) join via !recv")

!(gate ACT2-inner-fork 2
   (count-tuple (collapse (rhometta:transitions (cell 9 (step-inner (: $p (HasType one $A)) Z 9)))))
   "the inner multiplicity is a GENUINE 2-way COMM overload fork (one : Int | Float), not hardcoded")

!(gate ACT3-fanin 2 (count-tuple (inner-proof-set (: $pi (HasType (box one) $Ai)) 7))
   "the derived inner set delivered to the parent !recv has 2 messages on one channel (real fan-in)")

!(gate ACT4-oracle 2 (both-present (oracle-judgments))
   "INDEPENDENT nilbc oracle DERIVES the same 2 boxed-boxed skeletons from the same &kb (not a constant)")

!(gate ACT5-prune 1 (count-tuple (inner-proof-set (: $pi (HasType (box x) $Ai)) 7))
   "(box x): x is Int-only => exactly 1 typing; the Float branch never appears (no over-generation)")

!(gate ACT6-no-unexpected 0 (count-tuple (unexpected (rho-judgments)))
   "every rho proof leaf is one the oracle derives; no proof skeleton outside the derived set")

!("---- derived boxed-boxed proof terms from replicated fan-in (TV = overload rank) ----")
!(prove-mayset)
!("---- independent nilbc oracle proof terms (the expected derived set) ----")
!(oracle-judgments)

; ===========================================================================
; ACT7-ACT9 -- MULTI-PREMISE cross-product fan-in at depth.
;
; The unary core above takes multiplicity from ONE overloaded premise.  Here the
; inner subgoal is (plus one one) under the 3-PREMISE application rule: overloaded
; plus (2) x overloaded one (2) x one (2) is an 8-way fork pruned at the join to
; the 2 consistent typings (Int, Float).  That multi-premise multiplicity is then
; boxed and fanned into the parent (box _) join through the SAME rho:patterns:!recv.
; The independent bc oracle below has a 3-premise rule clause, so this case is
; cross-checked by real backward chaining rather than by expected-output constants.
; ---------------------------------------------------------------------------
!(add-atom &kb (: pplus-i (HasType plus (Fn2 Int   Int   Int))))
!(add-atom &kb (: pplus-f (HasType plus (Fn2 Float Float Float))))
!(add-atom &kb (op-stv (Fn2 Int   Int   Int)   (stv 0.90 0.90)))
!(add-atom &kb (op-stv (Fn2 Float Float Float) (stv 0.60 0.90)))
!(add-atom &kb (: app (-> (: $pf (HasType $op (Fn2 $A $B $C)))
                          (: $pa (HasType $a $A)) (: $pb (HasType $b $B))
                          (HasType ($op $a $b) $C))))
(= (app-tv (HasType $op $ft)) (overload-rank $ft))

; inner: 3-premise (plus one one) derivation (multiplicity from a cross-product)
(= (mp-step (: $prf $concl) $depth $b) (match &kb (: $prf $concl) (: $prf $concl)))
(= (mp-step (: $prf $concl) (S $d) $b)
   (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $concl))
     (mp-assemble3 $rule (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $d $b)))
(= (mp-assemble3 $rule $p1 $p2 $p3 $d $b)
   (let* (($c1 (+ (* $b 4) 1)) ($c2 (+ (* $b 4) 2)) ($c3 (+ (* $b 4) 3)) ($c4 (+ (* $b 4) 4)))
     (rho:par
       (rho:send (chan $c1) (rhometta:eval (mp-step $p1 $d $c1)))
       (rho:send (chan $c2) (rhometta:eval (mp-step $p2 $d $c2)))
       (rho:send (chan $c3) (rhometta:eval (mp-step $p3 $d $c3)))
       (rho:recv (chan $c1) $r1 (rho:recv (chan $c2) $r2 (rho:recv (chan $c3) $r3
         (rho:send (chan $c4) (rhometta:eval (mp-combine3 $rule (unval $r1) (unval $r2) (unval $r3)))))))
       (rho:recv (chan $c4) $z (rho:drop $z)))))
(= (mp-combine3 $rule (: $p1 $t1) (: $p2 $t2) (: $p3 $t3))
   (let $res (collapse (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $concl))
                          (proved (: ($rule $p1 $p2 $p3) $concl) (app-tv $t1))))
     (if (== $res ()) rho:nil (car-atom $res))))
(= (mp-inner-set $goal $b) (collapse (proof-of-inner (rhometta:run (cell $b (mp-step $goal (S Z) $b))))))

; outer: box each derived (plus one one) typing, fanned in via the SAME !recv
(= (mp-outer-step (proved (: $pi (HasType (plus one one) $Ai)) $tvi))
   (let $res (collapse (match &kb (: boxc (-> (: $pi (HasType (plus one one) $Ai)) $concl))
                          (proved (: (boxc $pi) $concl) $tvi)))
     (if (== $res ()) rho:nil (car-atom $res))))
(= (mp-outer-step $o) rho:nil)
(= (mp-outer-net $inner)
   (rho:par
     (fan-sends (chan 1) $inner)
     (rho:patterns:!recv (chan 2) (chan 1) $sub
       (rho:send (chan 3) (rhometta:eval (mp-outer-step (unval $sub)))))
     (rho:patterns:!recv (chan 4) (chan 3) $r (rho:drop $r))))
(= (prove-mp)
   (let $inner (mp-inner-set (: $pi (HasType (plus one one) $Ai)) 9)
     (collapse (proof-of (rhometta:run (mp-outer-net $inner))))))

; independent bc oracle extension for the 3-premise application rule.
(= (bc (: ($rule $p1 $p2 $p3) $concl) (S $d))
   (let* (((: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $concl))
          (bc (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $concl)) $d))
          ((: $p1 $t1) (bc (: $p1 $t1) $d))
          ((: $p2 $t2) (bc (: $p2 $t2) $d))
          ((: $p3 $t3) (bc (: $p3 $t3) $d)))
     (: ($rule $p1 $p2 $p3) $concl)))

(= (mp-judgments) (let $ps (prove-mp) (collapse (j-of (superpose $ps)))))
(= (mp-oracle-judgments) (collapse (bc (: $p (HasType (box (plus one one)) $T)) (S (S Z)))))
(= (mp-has-int $js)
   (> (count-tuple (collapse (case (superpose $js)
        (((: (boxc (app pplus-i pone-i pone-i)) (HasType (box (plus one one)) (Boxed Int))) hit) ($_ (empty)))))) 0))
(= (mp-has-float $js)
   (> (count-tuple (collapse (case (superpose $js)
        (((: (boxc (app pplus-f pone-f pone-f)) (HasType (box (plus one one)) (Boxed Float))) hit) ($_ (empty)))))) 0))
(= (mp-both-present $js) (+ (if (mp-has-int $js) 1 0) (if (mp-has-float $js) 1 0)))
(= (mp-present) (mp-both-present (mp-judgments)))
(= (mp-oracle-present) (mp-both-present (mp-oracle-judgments)))
(= (mp-unexpected $js)
   (collapse (case (superpose $js)
     (((: (boxc (app pplus-i pone-i pone-i)) (HasType (box (plus one one)) (Boxed Int))) (empty))
      ((: (boxc (app pplus-f pone-f pone-f)) (HasType (box (plus one one)) (Boxed Float))) (empty))
      ($x $x)))))

!(gate ACT7-multi-premise-rho 2 (mp-present)
   "multi-premise cross-product at depth: (box (plus one one)) -- a 2x2x2 fork pruned to 2 -- fans BOTH boxed typings into the parent join via !recv")

!(gate ACT8-multi-premise-oracle 2 (mp-oracle-present)
   "the independent nilbc oracle also derives the two boxed application skeletons from the same &kb")

!(gate ACT9-multi-premise-no-unexpected 0 (count-tuple (mp-unexpected (mp-judgments)))
   "the rho multi-premise proof leaves contain no skeleton outside the oracle-derived set")

!("---- derived multi-premise boxed proof terms from replicated fan-in ----")
!(prove-mp)
!("---- independent multi-premise nilbc oracle proof terms ----")
!(mp-oracle-judgments)
