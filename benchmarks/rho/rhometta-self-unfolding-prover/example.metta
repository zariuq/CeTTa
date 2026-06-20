; ===========================================================================
;  self_unfolding_prover.metta  --  a showcase for rhometta
;
;  THE IDEA IN ONE LINE:
;     proving a goal PLANTS the reactions that prove its subgoals.
;
;  rhometta lets a COMM rendezvous evaluate a deferred MeTTa term (eval-at-COMM,
;  the verified `eval_drop` bridge).  Here the deferred term is one backward-
;  chaining step, and its result is *more rho process* -- the cells for the
;  subgoals.  rhometta:run keeps reducing, so the reaction network UNFOLDS
;  ITSELF into a proof tree.  When a goal has several rules, the deferred eval
;  has several results, so the COMM forks: nondeterministic inference becomes
;  branching reduction, and rhometta:run returns the MAY-SET of every complete
;  proof tree.
;
;  communication  =  computation  =  construction.
;  Plain Rholang can pass names and spawn processes but has no
;  "evaluate-a-MeTTa-term-at-COMM"; a bare MeTTa interpreter has no concurrent
;  reduction, private channels, or may-set frontier.  rhometta is the fusion.
;
;  Self-contained: runs on the core build (no lib_pln, no MORK, no pathmap):
;     make BUILD=core && ./cetta self_unfolding_prover.metta
; ===========================================================================

!(import! &self rhometta)

; --- knowledge base: a goal-decomposition graph --------------------------
;   (prim X)      X is a primitive you already have (a proof leaf)
;   (rule H BODY) goal H holds when every subgoal in BODY holds
; (have batter) has TWO rules, so the proof network BRANCHES.
(prim flour) (prim egg) (prim oven) (prim mix) (prim bread) (prim cheese)

(rule (have cake)   ((have batter) (have oven)))
(rule (have batter) ((have flour) (have egg)))     ; bake it
(rule (have batter) ((have mix)))                  ; ...or use boxed mix
(rule (have sandwich) ((have bread) (have cheese))) ; single-rule => confluent

(= (is-prim (have $x))
   (if (== (collapse (match &self (prim $x) ok)) ()) False True))
(= (prim-name (have $x)) $x)

; --- rho plumbing ---------------------------------------------------------
; A strict-core rho channel is a quoted process; (chan n) mints distinct ones.
(= (chan $n)
   (if (== $n 0) (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; One eval-at-COMM cell: send the deferred job, receive it, *drop it to run it*.
; This is exactly the paper's eval_drop shape:  c!(eval J) | for(y<-c){ *y }.
(= (cell $i $job)
   (rho:par
     (rho:send (chan $i) (rhometta:eval $job))
     (rho:recv (chan $i) $y (rho:drop $y))))

; Turn a BODY tuple into the parallel cells for its subgoals.
; child index = parent*3 + k  keeps channels distinct across the whole tree.
(= (emit $body $b $k)
   (if (== $body ()) rho:nil
       (rho:par
         (cell (+ (* $b 3) $k) (expand (car-atom $body) (+ (* $b 3) $k)))
         (emit (cdr-atom $body) $b (+ $k 1)))))

; --- the one deferred inference step -------------------------------------
; A primitive returns a leaf (inert data -> rho:val).  A compound returns the
; cells for its subgoals (live rho process -> reduced further).  Several rules
; => several results => the COMM forks into the may-set.  expand is evaluated
; only AT COMM TIME (rhometta:eval defers it), never eagerly at the surface.
(= (expand $g $b)
   (if (is-prim $g)
       (leaf (prim-name $g))
       (match &self (rule $g $body) (emit $body $b 1))))

; ===========================================================================
;  ACT 1 -- run the whole search.  The seed is a SINGLE cell; everything else
;  is planted by the reactions themselves.  Each residual is one complete
;  proof tree, reported by the primitives it bottomed out at.
; ===========================================================================
!("ACT 1  proof may-set for (have cake)  -- expect 2 trees:")
!("        {flour, egg, oven}   and   {mix, oven}")
!(collapse (rhometta:run (cell 1 (expand (have cake) 1))))

; ===========================================================================
;  ACT 2 -- the branching is GENUINE REDUCTION, not post-hoc enumeration.
;  One step from the (have batter) goal shows two successor networks, one per
;  rule -- the fork happens in the reducer, at the COMM.
; ===========================================================================
!("ACT 2  one-step frontier of the (have batter) goal -- expect 2 successors:")
!(collapse (rhometta:transitions (cell 1 (expand (have batter) 1))))

; ===========================================================================
;  ACT 3 -- you choose determinism by TOPOLOGY.  A goal with a single rule is
;  confluent-by-construction: exactly one residual, no fork.
; ===========================================================================
!("ACT 3  a single-rule goal is confluent -- expect exactly 1 residual:")
!("        {bread, cheese}")
!(collapse (rhometta:run (cell 1 (expand (have sandwich) 1))))
