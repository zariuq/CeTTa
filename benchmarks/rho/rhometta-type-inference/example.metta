; ===========================================================================
;  rhometta-type-inference  --  Curry-Howard type inference AS concurrent
;  backward chaining.  The flagship rhometta demo.
;
;  Type-checking IS proof search: a typing judgment (: proof (HasType expr ty))
;  is proved by backward chaining over typing rules, and the proof term IS the
;  typing derivation.  Each resolution step is one eval-at-COMM, so:
;     * AND  -- an application's subgoals (the operator's type and each argument's
;               type) are inferred in PARALLEL rho cells;
;     * OR   -- an overloaded operator gives several typings => a genuine
;               rhometta may-set (one branch per overload);
;     * JOIN -- the typing rule is re-matched against the resolved premises, so
;               the shared type variables A,B,C are unified; an overload whose
;               domains do not fit the arguments fails to unify and the branch
;               reduces cleanly to rho:nil (no dangling rendezvous).
;
;  Curry-Howard proof terms stay PURE: a proof is exactly (: (app pf pa pb) Concl).
;  A PLN-style truth value rides OUTSIDE, in (proved <judgment> <stv>); here it
;  ranks competing overloads (Int addition preferred over Float) -- derivation
;  metadata, never part of the typing judgment.
;
;  We use a 2-argument application rule, so the showcase examples are one
;  resolution step deep -- enough to exhibit may-set, parallel AND-subgoals, and
;  join-time pruning, without recursion.  (rhometta-typed-bc shows the recursive
;  shape; the deep-recursion-with-join variant is future work, see description.txt.)
;
;  KB lives in &kb (a new-space) so the evaluator does not type-CHECK the proof
;  term (app pf pa pb) as an application (the dependent domain (: $pf (HasType ..))
;  would trip BadArgType in &self without the HE' telescope).
;
;  Runs on the core build:  make BUILD=core
;     ./cetta --profile he-extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)

; --- typing context, as Curry-Howard judgments (: proofname (HasType expr ty)) -
!(bind! &kb (new-space))
!(add-atom &kb (: px     (HasType x    Int)))
!(add-atom &kb (: pone-i (HasType one  Int)))
!(add-atom &kb (: pone-f (HasType one  Float)))    ; one is overloaded Int | Float
!(add-atom &kb (: ptrue  (HasType true Bool)))     ; for the ill-typed canary
; plus is overloaded: (Fn2 Int Int Int) and (Fn2 Float Float Float).
!(add-atom &kb (: pplus-i (HasType plus (Fn2 Int   Int   Int))))
!(add-atom &kb (: pplus-f (HasType plus (Fn2 Float Float Float))))
; overload ranking (PLN metadata): prefer Int addition over Float.
!(add-atom &kb (op-stv (Fn2 Int   Int   Int)   (stv 0.90 0.90)))
!(add-atom &kb (op-stv (Fn2 Float Float Float) (stv 0.60 0.90)))

; THE typing rule: application elimination for a 2-argument operator.
;   pf : the operator has a 2-arg function type (Fn2 A B C)
;   pa : argument 1 has type A,   pb : argument 2 has type B
;   ===========================================================================
;   (op a b) has type C,  with proof term (app pf pa pb).
; A, B, C are SHARED across premises and conclusion -- the join keeps them
; consistent, which is exactly what prunes the overloads that do not fit.
!(add-atom &kb (: app (-> (: $pf (HasType $op (Fn2 $A $B $C)))
                          (: $pa (HasType $a $A))
                          (: $pb (HasType $b $B))
                          (HasType ($op $a $b) $C))))

; --- rho plumbing ------------------------------------------------------------
(= (chan $n)
   (if (== $n 0) (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))
(= (unval (rho:quote (rho:val $x))) $x)
(= (cell $i $job)
   (rho:par (rho:send (chan $i) (rhometta:eval $job))
            (rho:recv (chan $i) $y (rho:drop $y))))

; one resolution step: BASE = a typing fact; RULE = the application rule (3 premises).
(= (step (: $prf $concl) $depth $b)
   (match &kb (: $prf $concl) (: $prf $concl)))
(= (step (: $prf $concl) (S $d) $b)
   (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $concl))
     (assemble3 $rule (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $d $b)))

; AND-node: infer the THREE subgoals in PARALLEL cells, then join.
(= (assemble3 $rule $prem1 $prem2 $prem3 $d $b)
   (let* (($c1 (+ (* $b 4) 1)) ($c2 (+ (* $b 4) 2)) ($c3 (+ (* $b 4) 3)) ($c4 (+ (* $b 4) 4)))
     (rho:par
       (rho:send (chan $c1) (rhometta:eval (step $prem1 $d $c1)))
       (rho:send (chan $c2) (rhometta:eval (step $prem2 $d $c2)))
       (rho:send (chan $c3) (rhometta:eval (step $prem3 $d $c3)))
       (rho:recv (chan $c1) $r1
         (rho:recv (chan $c2) $r2
           (rho:recv (chan $c3) $r3
             (rho:send (chan $c4)
               (rhometta:eval (combine3 $rule (unval $r1) (unval $r2) (unval $r3)))))))
       (rho:recv (chan $c4) $z (rho:drop $z)))))

; JOIN + unification guard, made TOTAL.  Re-match the rule against the resolved
; premises: the shared A,B,C force consistency.  Compatible => a proof (ranked by
; the overload's truth value); an overload that does not fit => rho:nil.
(= (combine3 $rule (: $p1 $t1) (: $p2 $t2) (: $p3 $t3))
   (let $res (collapse (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $concl))
                          (proved (: ($rule $p1 $p2 $p3) $concl) (app-tv $t1))))
     (if (== $res ()) rho:nil (car-atom $res))))
; truth value of an application = the overload rank of the operator's type.
(= (app-tv (HasType $op $ft)) (overload-rank $ft))
(= (overload-rank $ft)
   (let $r (collapse (match &kb (op-stv $ft $tv) $tv))
     (if (== $r ()) (stv 1.0 0.90) (car-atom $r))))

; --- projections over the may-set --------------------------------------------
(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))
(= (proof-of (rho:val (proved $j $tv))) (proved $j $tv))
(= (proof-of $other) (empty))
(= (judgment-of (rho:val (proved $j $tv))) $j)
(= (judgment-of $other) (empty))
(= (stuck-of $r)
   (case $r (((rho:val (proved $j $tv)) (empty)) (rho:nil (empty)) ($other $other))))

; --- ORACLE: sequential nilbc-style chainer over the SAME &kb -----------------
(= (bc $kb $_ (: $proof $theorem))
   (match $kb (: $proof $theorem) (: $proof $theorem)))
(= (bc $kb (S $depth) (: ($rule $p1 $p2 $p3) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) $theorem))))
          ((: $p1 $t1) (bc $kb $depth (: $p1 $t1)))
          ((: $p2 $t2) (bc $kb $depth (: $p2 $t2)))
          ((: $p3 $t3) (bc $kb $depth (: $p3 $t3))))
     (: ($rule $p1 $p2 $p3) $theorem)))

; --- readable harness: one compact line per ACT, no [()] wall ----------------
(= (verdict $exp $got) (if (== $exp $got) PASS *FAIL*))
(= (run-proofs $goal) (collapse (proof-of (rhometta:run (cell 1 (step $goal (S Z) 1))))))
(= (act $tag $exp $goal $desc)
   (let $ps (run-proofs $goal)
     (=== $tag (verdict $exp (count-tuple $ps)) expect $exp got (count-tuple $ps) : $desc :: $ps)))
(= (gate $tag $exp $got $desc)
   (=== $tag (verdict $exp $got) expect $exp got $got : $desc))

; ===========================================================================
!(act ACT1-may-set 2 (: $prf (HasType (plus one one) $T))
   "overloaded (plus one one) => 2 typings, ranked Int(.9) > Float(.6)")

!(act ACT2-prune 1 (: $prf (HasType (plus x one) $T))
   "(plus x one): x is Int-only, so the Float overload is pruned at the join => 1 typing")

!(act ACT3-neg 0 (: $prf (HasType (plus true one) $T))
   "(plus true one): true is Bool, no plus overload fits => 0 typings (bad branch vanishes)")

!(gate ACT4-no-stuck 0
   (count-tuple (collapse (stuck-of (rhometta:run
      (cell 1 (step (: $prf (HasType (plus one one) $T)) (S Z) 1))))))
   "(plus one one) leaves NO dangling rho:recv/send/eval-payload (only proofs + clean rho:nil)")

!(gate ACT5-fork 2
   (count-tuple (collapse (rhometta:transitions
      (cell 1 (step (: $p (HasType plus (Fn2 $A $B $C))) (S Z) 1)))))
   "the overload lookup is a GENUINE COMM fork: 2 successors (Int and Float plus)")

!(gate ACT6-oracle
   (count-tuple (collapse (bc &kb (S Z) (: $prf (HasType (plus one one) $T)))))
   (count-tuple (run-proofs (: $prf (HasType (plus one one) $T))))
   "sequential nilbc oracle on (plus one one) agrees on the proof count")

!("---- proof terms (Curry-Howard derivations; TV = overload rank) ----")
!("(plus one one):")
!(run-proofs (: $prf (HasType (plus one one) $T)))
!("(plus x one)  -- Float pruned, Int survives:")
!(run-proofs (: $prf (HasType (plus x one) $T)))
