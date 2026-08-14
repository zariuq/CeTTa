; ===========================================================================
;  self_unfolding_prover.metta  --  proof search as a self-growing reaction net
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
;  Plain rho can pass names and spawn processes but has no
;  "evaluate-a-MeTTa-term-at-COMM"; a bare MeTTa interpreter has no concurrent
;  reduction, private channels, or may-set frontier.  rhometta is the fusion.
;
;  Self-contained: runs on the core build (no lib_pln, no MORK, no pathmap):
;     make BUILD=core && ./cetta --quiet --profile he-extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)

; Standard rho notation used below:
;   P | Q                 parallel composition
;   x!(m)                send process m on channel x
;   for (y <- x) { P }  receive from x, bind y in P
;   *y                   drop: run the quoted process/name y

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
; Standard reading of one emitted child:
;   c_k!(eval(expand subgoal)) | for (y <- c_k) { *y }
; emit(body,...) is the parallel composition of all such child cells.
(= (emit $body $b $k)
   (if (== $body ()) rho:nil
       (rho:par
         (cell (+ (* $b 3) $k) (expand (car-atom $body) (+ (* $b 3) $k)))
         (emit (cdr-atom $body) $b (+ $k 1)))))

; --- the one deferred inference step -------------------------------------
; A primitive returns a leaf (inert data -> rho:val).  A compound returns the
; cells for its subgoals (live rho process -> reduced further).  Several rules
; => several results => the COMM forks into the may-set.  expand is evaluated
; only AT COMM TIME (rhometta:eval defers it), never eagerly at the syntax.
(= (expand $g $b)
   (if (is-prim $g)
       (leaf (prim-name $g))
       (match &self (rule $g $body) (emit $body $b 1))))

; --- readable output: show each proof tree as the SET of primitives it needs ---
(= (leaf-of $r)
   (case $r
     (((rho:val (leaf $x)) $x)
      ((rho:par $a $b) (superpose ((leaf-of $a) (leaf-of $b))))
      ((rho:par $a $b $c) (superpose ((leaf-of $a) (leaf-of $b) (leaf-of $c))))
      ((rho:par $a $b $c $d) (superpose ((leaf-of $a) (leaf-of $b) (leaf-of $c) (leaf-of $d))))
      ($_ (empty)))))
(= (tree-of $r) (collapse (leaf-of $r)))

; ===========================================================================
;  ACT 1 -- run the whole search from a SINGLE seed cell; the reactions plant
;  everything else.  (have batter) has two rules, so (have cake) grows TWO proof
;  trees, each shown as the primitives it bottoms out at.
; ===========================================================================
!("(have cake): proof trees -- expect two, {flour egg oven} and {mix oven}:")
!(tree-of (rhometta:run (cell 1 (expand (have cake) 1))))

; ===========================================================================
;  ACT 2 -- a goal with a single rule is confluent: exactly one tree.
; ===========================================================================
!("(have sandwich): one rule => one tree -- expect {bread cheese}:")
!(tree-of (rhometta:run (cell 1 (expand (have sandwich) 1))))
