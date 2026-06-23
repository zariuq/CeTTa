; ===========================================================================
;  rhometta-typed-bc  --  backward chaining in the STANDARD Hyperon idiom,
;  run as concurrent rho reactions.
;
;  This ports the self-unfolding prover from its bespoke (have X)/(rule H B)
;  encoding into the representation Nil Geisweiller's nilbc, Patrick Hammer's
;  lib_pln, and the PeTTa chainer all use:
;     facts   (: <name> <type>)
;     rules   (: <r> (-> (: $p1 P1) (: $p2 P2) Concl))
;     proofs  Curry-Howard terms  (: (<r> p1 p2) Concl)
;  Each backward-chaining resolution step is ONE eval-at-COMM; proving a goal
;  plants the reactions that prove its premises; rhometta:run returns the
;  may-set of complete typed proofs.  Cf. tests/bench_backchain_heavy_nilbc.metta
;  (the same mortal KB, run by the sequential nilbc `bc`, used here as an oracle).
;
;  Two design points learned the hard way (documented so they are not relearned):
;
;  (1) The KB lives in a SEPARATE space (&kb), exactly as the nilbc bench does.
;      A proof term like (r-mortal r01 r01c) has r-mortal in function-head
;      position; if r-mortal's arrow type were in &self the evaluator would
;      type-CHECK it as an application and (without the HE' dependent telescope)
;      reject the dependent domain (: $r (researcher $x)) with BadArgType.  With
;      the rule in &kb, r-mortal is an unknown head in &self, so the proof term
;      stays inert data -- which is what a proof term should be.
;
;  (2) The Curry-Howard judgment stays PURE: a proof is exactly
;      (: (r p1 p2) Concl), the same term nilbc produces.  The PLN truth value
;      rides OUTSIDE it, in a (proved <judgment> <stv>) wrapper -- evidence
;      metadata (confidence / rank of the derivation), NOT a claim that the type
;      judgment is probabilistic.  A Horn rule (premises -> conclusion) is not a
;      syllogism, so we do NOT mis-apply lib_pln's Truth_Deduction (which is for
;      A->B, B->C |- A->C); the premises here are ground facts (certain), so a
;      proof's truth value is just the rule's own strength.  See
;      rhometta-type-inference for where competing derivations make it informative.
;
;  Shared variables (e.g. r-mortal shares $x across researcher/curious/mortal) are
;  the real hazard the untyped cake never had: the two premise cells run in
;  PARALLEL, so a free-variable goal would pair researcher-r01 with curious-e01.
;  The join (combine2) re-matches the rule against the resolved (ground) premises;
;  the rule's shared $x then forces agreement.  combine2 is TOTAL: a consistent
;  pair becomes (proved ...), an inconsistent pair becomes rho:nil so the branch
;  reduces cleanly to the empty process instead of leaving a dangling rendezvous.
;
;  Runs on the core build:  make BUILD=core
;     ./cetta --profile he-extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)

; --- KB: nilbc (: name type) facts + (: r (-> premises concl)) rules, in &kb ---
!(bind! &kb (new-space))
!(add-atom &kb (: r01  (researcher r01)))   !(add-atom &kb (: r01c (curious r01)))
!(add-atom &kb (: e01  (engineer e01)))     !(add-atom &kb (: e01c (curious e01)))
!(add-atom &kb (: b01  (researcher b01)))   ; researcher but NOT curious -> negative canary

; mortal(x) if researcher(x) ^ curious(x)  -- OR --  engineer(x) ^ curious(x).
; Two rules => the goal (mortal $x) BRANCHES (the OR may-set).
!(add-atom &kb (: r-mortal (-> (: $r (researcher $x)) (: $c (curious $x)) (mortal $x))))
!(add-atom &kb (: e-mortal (-> (: $e (engineer $x))   (: $c (curious $x)) (mortal $x))))
!(add-atom &kb (rule-stv r-mortal (stv 0.90 0.80)))
!(add-atom &kb (rule-stv e-mortal (stv 0.85 0.80)))

; a single-rule recipe goal (ACT 7): confluent-by-topology => one proof.
!(add-atom &kb (: f1 (have-flour)))  !(add-atom &kb (: g1 (have-egg)))
!(add-atom &kb (: bake (-> (: $f (have-flour)) (: $e (have-egg)) (have-batter))))
!(add-atom &kb (rule-stv bake (stv 0.95 0.90)))

; --- rho plumbing (chan/cell/unval as in the other rhometta demos) -----------
(= (chan $n)
   (if (== $n 0) (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))
(= (unval (rho:quote (rho:val $x))) $x)   ; recover a child's value from its COMM carrier

; one eval-at-COMM cell (the eval_drop shape): defer job, receive, drop-to-run.
(= (cell $i $job)
   (rho:par (rho:send (chan $i) (rhometta:eval $job))
            (rho:recv (chan $i) $y (rho:drop $y))))

; --- one backward-chaining resolution step = one deferred payload ------------
; BASE (any depth): the goal resolves directly against a fact in &kb.  Multiple
; matching facts => the COMM forks (OR).
(= (step (: $prf $concl) $depth $b)
   (match &kb (: $prf $concl) (: $prf $concl)))
; RULE (depth S d): pick a binary rule whose conclusion unifies the goal, then
; plant a cell per premise and assemble.  Several rules => the COMM forks (OR).
(= (step (: $prf $concl) (S $d) $b)
   (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) $concl))
     (assemble2 $rule (: $p1 $t1) (: $p2 $t2) $d $b)))

; AND-node: prove both premises in PARALLEL cells, then join.
(= (assemble2 $rule $prem1 $prem2 $d $b)
   (let* (($c1 (+ (* $b 3) 1)) ($c2 (+ (* $b 3) 2)) ($c3 (+ (* $b 3) 3)))
     (rho:par
       (rho:send (chan $c1) (rhometta:eval (step $prem1 $d $c1)))
       (rho:send (chan $c2) (rhometta:eval (step $prem2 $d $c2)))
       (rho:recv (chan $c1) $r1
         (rho:recv (chan $c2) $r2
           (rho:send (chan $c3)
             (rhometta:eval (combine2 $rule (unval $r1) (unval $r2))))))
       (rho:recv (chan $c3) $z (rho:drop $z)))))

; The join + shared-variable GUARD, made TOTAL.  Re-match the rule against the
; RESOLVED (ground) premises: the rule's shared $x forces consistency.  Consistent
; => (proved <pure judgment> <stv>); inconsistent => rho:nil (the branch reduces
; to the empty process; no dangling send/recv pollutes the may-set).
(= (combine2 $rule (: $p1 $t1) (: $p2 $t2))
   (let $res (collapse (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) $concl))
                          (proved (: ($rule $p1 $p2) $concl) (proof-tv $rule))))
     (if (== $res ()) rho:nil (car-atom $res))))
(= (proof-tv $rule) (match &kb (rule-stv $rule $tv) $tv))

; size of a collapsed tuple (count-tuple lives in lib_pln; we stay dependency-free).
(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))

; --- projections over the may-set --------------------------------------------
; keep only completed proofs; drop the benign rho:nil pruned-branch residuals.
(= (proof-of (rho:val (proved $j $tv))) (proved $j $tv))
(= (proof-of $other) (empty))
; strip the PLN wrapper to the bare Curry-Howard judgment (for the oracle).
(= (judgment-of (rho:val (proved $j $tv))) $j)
(= (judgment-of $other) (empty))
; keep ONLY residuals that are neither a proof nor rho:nil -- i.e. stuck junk.
; The may-set must contain none (no dangling rho:recv/rho:send/rho:eval-payload).
; `case` is first-match deterministic, unlike open-world `=` clauses which all fire.
(= (stuck-of $r)
   (case $r
     (((rho:val (proved $j $tv)) (empty))
      (rho:nil (empty))
      ($other $other))))

; --- ORACLE: the sequential nilbc backward chainer over the SAME &kb ----------
; Its `let*` threads the shared $x between premises, so its proof terms are the
; ground truth the rho reaction encoding must reproduce.
(= (bc $kb $_ (: $proof $theorem))
   (match $kb (: $proof $theorem) (: $proof $theorem)))
(= (bc $kb (S $depth) (: ($rule $p1 $p2) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) (: $p2 $t2) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) (: $p2 $t2) $theorem))))
          ((: $p1 $t1) (bc $kb $depth (: $p1 $t1)))
          ((: $p2 $t2) (bc $kb $depth (: $p2 $t2))))
     (: ($rule $p1 $p2) $theorem)))

; --- readable harness: one compact line per ACT, no [()] wall ----------------
(= (verdict $exp $got) (if (== $exp $got) PASS *FAIL*))
(= (run-proofs $goal) (collapse (proof-of (rhometta:run (cell 1 (step $goal (S Z) 1))))))
(= (act $tag $exp $goal $desc)
   (let $ps (run-proofs $goal)
     (=== $tag (verdict $exp (count-tuple $ps)) expect $exp got (count-tuple $ps) : $desc :: $ps)))
(= (gate $tag $exp $got $desc)
   (=== $tag (verdict $exp $got) expect $exp got $got : $desc))

; ===========================================================================
!(act ACT1-may-set 2 (: $prf (mortal $x))
   "typed-proof may-set of (mortal $x): r-mortal and e-mortal (pure proof terms, TV outside)")

!(act ACT2-neg-canary 0 (: $prf (mortal b01))
   "(mortal b01): b01 is a researcher but NOT curious => 0 proofs (incomplete evidence)")

!(gate ACT3-guard-gate 2
   (count-tuple (collapse (judgment-of (rhometta:run (cell 1 (step (: $prf (mortal $x)) (S Z) 1))))))
   "shared $x must agree across the parallel premise cells (an unguarded cross-product would give 6)")

!(gate ACT4-no-stuck 0
   (count-tuple (collapse (stuck-of (rhometta:run (cell 1 (step (: $prf (mortal $x)) (S Z) 1))))))
   "no dangling rho:recv/send/eval-payload (every residual is a proof or a clean rho:nil)")

!(gate ACT5-fork 2
   (count-tuple (collapse (rhometta:transitions (cell 1 (step (: $prf (mortal $x)) (S Z) 1)))))
   "the two rules are a GENUINE one-step COMM fork: 2 successor networks")

!(gate ACT6-oracle
   (count-tuple (collapse (bc &kb (S Z) (: $prf (mortal $x)))))
   (count-tuple (run-proofs (: $prf (mortal $x))))
   "sequential nilbc bc over the same KB agrees on the proof count + terms")

!(act ACT7-confluent 1 (: $prf (have-batter))
   "single-rule (have-batter) is confluent-by-topology => exactly 1 proof")

!("---- nilbc oracle proof terms (must match the rhometta judgments above) ----")
!(collapse (bc &kb (S Z) (: $prf (mortal $x))))
