; ===========================================================================
;  rhometta-typed-bc  --  backward chaining with typed (Curry-Howard) proofs,
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
;  Two notes:
;
;  (1) The KB lives in a separate space (&kb) so the evaluator treats a proof term
;      like (r-mortal r01 r01c) as inert data rather than a function call to
;      type-check.
;
;  (2) The Curry-Howard judgment is crisp: a proof is exactly
;      (: (r p1 p2) Concl), the same term nilbc produces -- a derivation either
;      holds or it does not, so no truth value is attached.  (For a genuine,
;      *computed* truth value over evidence, see rhometta-evidence-revision.)
;
;  Shared variables (e.g. r-mortal shares $x across researcher/curious/mortal) are
;  the real hazard the untyped cake never had: the two premise cells run in
;  PARALLEL, so a free-variable goal would pair researcher-r01 with curious-e01.
;  The join (combine2) re-matches the rule against the resolved (ground) premises;
;  the rule's shared $x then forces agreement.  combine2 is TOTAL: a consistent
;  pair becomes the proof term, an inconsistent pair becomes rho:nil so the branch
;  reduces cleanly to the empty process instead of leaving a dangling rendezvous.
;
;  Runs on the core build:  make BUILD=core
;     ./cetta --quiet --profile he-extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)

; Standard rho notation used below:
;   P | Q                 parallel composition
;   x!(m)                send process m on channel x
;   for (y <- x) { P }  receive from x, bind y in P
;   *y                   drop: run the quoted process/name y

; --- KB: nilbc (: name type) facts + (: r (-> premises concl)) rules, in &kb ---
!(bind! &kb (new-space))
!(add-atom &kb (: r01  (researcher r01)))   !(add-atom &kb (: r01c (curious r01)))
!(add-atom &kb (: e01  (engineer e01)))     !(add-atom &kb (: e01c (curious e01)))
!(add-atom &kb (: b01  (researcher b01)))   ; researcher but NOT curious -> negative canary

; mortal(x) if researcher(x) ^ curious(x)  -- OR --  engineer(x) ^ curious(x).
; Two rules => the goal (mortal $x) BRANCHES (the OR may-set).
!(add-atom &kb (: r-mortal (-> (: $r (researcher $x)) (: $c (curious $x)) (mortal $x))))
!(add-atom &kb (: e-mortal (-> (: $e (engineer $x))   (: $c (curious $x)) (mortal $x))))

; a single-rule recipe goal (ACT 7): confluent-by-topology => one proof.
!(add-atom &kb (: f1 (have-flour)))  !(add-atom &kb (: g1 (have-egg)))
!(add-atom &kb (: bake (-> (: $f (have-flour)) (: $e (have-egg)) (have-batter))))

; --- rho plumbing (chan/cell/unval as in the other rhometta demos) -----------
(= (chan $n)
   (if (== $n 0) (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))
(= (unval (rho:quote (rho:val $x))) $x)   ; recover a child's value from its COMM carrier

; one eval-at-COMM cell (the eval_drop shape): defer job, receive, drop-to-run.
; Standard reading:
;   cell_i(job)  :=  c_i!(eval(job)) | for (y <- c_i) { *y }
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
; Standard rho shape:
;   c1!(eval(step prem1)) | c2!(eval(step prem2))
; | for (r1 <- c1) { for (r2 <- c2) {
;       c3!(eval(combine2 rule (unval r1) (unval r2)))
;   }}
; | for (z <- c3) { *z }
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
; => the bare proof term; inconsistent => rho:nil (the branch reduces
; to the empty process; no dangling send/recv pollutes the may-set).
(= (combine2 $rule (: $p1 $t1) (: $p2 $t2))
   (let $res (collapse (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) $concl))
                          (: ($rule $p1 $p2) $concl)))
     (if (== $res ()) rho:nil (car-atom $res))))

; size of a collapsed tuple (count-tuple lives in lib_pln; we stay dependency-free).
(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))

; --- projections over the may-set --------------------------------------------
; keep only completed proofs; drop the benign rho:nil pruned-branch residuals.
(= (proof-of (rho:val (: $p $c))) (: $p $c))
(= (proof-of $other) (empty))
; the proof term IS the bare Curry-Howard judgment (used by the oracle check).
(= (judgment-of (rho:val (: $p $c))) (: $p $c))
(= (judgment-of $other) (empty))
; keep ONLY residuals that are neither a proof nor rho:nil -- i.e. stuck junk.
; The may-set must contain none (no dangling rho:recv/rho:send/rho:eval-payload).
; `case` is first-match deterministic, unlike open-world `=` clauses which all fire.
(= (stuck-of $r)
   (case $r
     (((rho:val (: $p $c)) (empty))
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
     (=== $tag (verdict $exp (count-tuple $ps)) expect $exp got (count-tuple $ps) : $desc)))
(= (gate $tag $exp $got $desc)
   (=== $tag (verdict $exp $got) expect $exp got $got : $desc))

; ===========================================================================
!(act ACT1-may-set 2 (: $prf (mortal $x))
   "typed-proof may-set of (mortal $x): r-mortal and e-mortal (pure Curry-Howard proof terms)")

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
