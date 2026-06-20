; Rhometta showcase for Hyperon/AGI developers
; rho supplies the coordination graph; MeTTa payloads run only when COMM fires.
; Three cognitive services run in parallel:
;   1. a PLN-style two-hop proof search with multiple possible proof paths,
;   2. overlap-corrected evidence revision from two agents,
;   3. one ECAN-style attention-spreading round.
; Their owned results are gathered through rho channels and used by a final deferred
; MeTTa payload to synthesize an action plan.  The final may-set has one plan per
; proof path, showing faithful branching plus causal chaining.

!(import! &self rhometta)
!(import! &self lib_pln)
!(import! &self lib_overlap_corrected_revision)

; ---------------------------------------------------------------------------
; Rho channel generator.  These are pure rho names; no global mutable queue.
(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; ---------------------------------------------------------------------------
; Service 1: PLN proof search.  There are two A->C proof paths.
(link (Inheritance AGI Rhometta) (stv 0.82 0.90))
(link (Inheritance Rhometta ParallelControl) (stv 0.78 0.86))
(link (Inheritance AGI Hyperon) (stv 0.74 0.88))
(link (Inheritance Hyperon ParallelControl) (stv 0.84 0.80))

(= (prove-capability $a $c)
   (match &self (link (Inheritance $a $b) $t1)
     (match &self (link (Inheritance $b $c) $t2)
       (proof (Inheritance $a $c) (via $b)
              (Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.9) (stv 0.5 0.9)
                               $t1 $t2)))))

; ---------------------------------------------------------------------------
; Service 2: two agents report partially overlapping evidence; revise honestly.
(obs agent-a e1 supports) (obs agent-a e2 supports) (obs agent-a e3 supports) (obs agent-a e4 opposes)
(obs agent-b e3 supports) (obs agent-b e4 opposes) (obs agent-b e5 supports) (obs agent-b e6 supports)
(stamp-of agent-a (e1 e2 e3 e4))
(stamp-of agent-b (e3 e4 e5 e6))

(= (agent-evidence $agent)
   (let* (($pos (TupleCount (collapse (match &self (obs $agent $e supports) $e))))
          ($neg (TupleCount (collapse (match &self (obs $agent $e opposes) $e))))
          ($n (+ $pos $neg))
          ($stamp (match &self (stamp-of $agent $s) $s)))
         (evidence $agent (stv (/ $pos $n) (/ $n (+ $n 1))) $stamp)))

(= (revise-agents)
   (let* (((evidence agent-a $ta $sa) (agent-evidence agent-a))
          ((evidence agent-b $tb $sb) (agent-evidence agent-b)))
         (revision
           (overlap-corrected (Truth_OverlapCorrectedRevision $ta $tb $sa $sb))
           (naive (Truth_Revision $ta $tb))
           (shared-stamps (StampIntersectionCount $sa $sb)))))

; ---------------------------------------------------------------------------
; Numeric helper for tuple-valued match results.
(= (sum-tuple $xs)
   (if (== $xs ()) 0.0 (+ (car-atom $xs) (sum-tuple (cdr-atom $xs)))))

; ---------------------------------------------------------------------------
; Service 3: one ECAN-style attention-spreading round over an association graph.
(sti AGI 60.0) (sti Rhometta 20.0) (sti Hyperon 10.0) (sti ParallelControl 5.0) (sti ProofSearch 5.0)
(hebbian AGI Rhometta 0.9)
(hebbian AGI Hyperon 0.6)
(hebbian Rhometta ParallelControl 0.8)
(hebbian Hyperon ParallelControl 0.4)
(hebbian ParallelControl ProofSearch 0.7)
(= (max-spread) 0.4)

(= (sti-of $x)
   (match &self (sti $x $v) $v))
(= (out-total $x)
   (sum-tuple (collapse (match &self (hebbian $x $y $w) $w))))
(= (incoming $x)
   (sum-tuple (collapse
     (match &self (hebbian $y $x $w)
       (* (* (max-spread) (sti-of $y)) (/ $w (out-total $y)))))))
(= (keep $x)
   (if (> (out-total $x) 0.0)
       (* (- 1.0 (max-spread)) (sti-of $x))
       (sti-of $x)))
(= (new-sti $x)
   ($x (+ (keep $x) (incoming $x))))
(= (attention-report)
   (attention-round (new-sti AGI) (new-sti Rhometta) (new-sti Hyperon)
                    (new-sti ParallelControl) (new-sti ProofSearch)))

; ---------------------------------------------------------------------------
; Final synthesizer.  COMM gives each receive variable a quoted rho payload;
; unwrap the rho:val carrier to recover the MeTTa value.
(= (unval (rho:quote (rho:val $x))) $x)

(= (synthesize-plan $proof-q $revision-q $attention-q)
   (let* (($proof (unval $proof-q))
          ($revision (unval $revision-q))
          ($attention (unval $attention-q)))
      (agi-workflow-plan
        (selected-proof $proof)
        (belief $revision)
        (attention $attention)
        (next-step (spawn-specialists revise-beliefs allocate-attention continue-proof-search)))))

; ---------------------------------------------------------------------------
; The rho orchestration graph.  The first three services are independent and can
; fire in the same frontier.  The final service is causally downstream of their
; three owned results.
(= (agi-workflow)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (prove-capability AGI ParallelControl)))
     (rho:send (chan 2) (rhometta:eval (revise-agents)))
     (rho:send (chan 3) (rhometta:eval (attention-report)))
     (rho:recv (chan 1) $proof
       (rho:recv (chan 2) $belief
         (rho:recv (chan 3) $attn
           (rho:send (chan 4) (rhometta:eval (synthesize-plan $proof $belief $attn))))))
     (rho:recv (chan 4) $plan (rho:drop $plan))))

; The result set has one final plan for each proof path.
!(assertEqual (TupleCount (collapse (rhometta:run (agi-workflow)))) 2)

; Show the plans to the user.
!(collapse (rhometta:run (agi-workflow)))
