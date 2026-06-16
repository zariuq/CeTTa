; ===========================================================================
; DRAFT  --  control workload (A): compute / iteration-bound, MINIMAL heap churn
; ---------------------------------------------------------------------------
; STATUS: DRAFT for the hyperpose threaded-scaling investigation. Sizes are a
; first cut and MUST be calibrated on a QUIET machine before any headline
; numbers. Pairs with  alloc_mapreduce_farm_draft.metta  (workload B).
;
; PURPOSE: bracket the open question -- is the poor 8-thread hyperpose scaling
; caused by the per-call match/bindings HEAP traffic (glibc arena-lock
; contention), or by the threading substrate itself (scheduler/joins)?
;
;   * (A) scales ~linearly to 8T but (B) does NOT  -> allocation / allocator
;     lock is the bottleneck  (the working hypothesis).
;   * BOTH scale poorly                            -> the substrate is
;     implicated -> a different fix is needed.
;
; This file (A) is the LOW-allocation arm: a tight arithmetic loop that builds
; NO Cons cells / lists, so per-step heap churn is near the floor for a
; user-defined recursive function.
;
; DESIGN NOTES (verified against this engine on 2026-06-07):
;   * No TCO. Recursion depth is a logical limit costing ~2 units per level.
;     At max-stack-depth 400000, a single loop of ~80000-100000 steps is safe;
;     deeper needs a bigger max-stack-depth (which costs memory).
;   * Each branch uses a DISTINCT base offset, so no memoization/tabling can
;     short-circuit the work (identical calls could otherwise be cached).
;   * `collapse` forces ALL branches to run fully (steady-state throughput);
;     `once` would race-cancel after the first success (a different code path).
;
; HOW TO RUN THE LADDER (gently, on a quiet machine):
;   for T in 1 2 4 8; do
;     # set the num-threads pragma below to $T (or generate per-thread copies)
;     /usr/bin/time -v ./cetta benchmarks/scaling/ep_arith_farm_draft.metta \
;       2>&1 | grep -E "Elapsed|Voluntary con"
;   done
;   Compare wall-clock AND "Voluntary context switches" -- the latter is the
;   stable contention signal (clean runs show tens; contention shows millions).
;
; CALIBRATION KNOBS:
;   * SPAN below (per-branch step count). Keep SPAN <= ~100000 at the current
;     max-stack-depth. For a longer run, raise BOTH max-stack-depth and SPAN
;     together (memory cost), or add branches (less useful past core count).
; ===========================================================================

!(pragma! max-stack-depth 400000)

; arith loop: accumulate i*i for i in [start, stop).  Pure grounded arithmetic;
; no lists are constructed -> minimal per-call heap allocation.
(= (arith $i $stop $acc)
   (if (>= $i $stop)
       $acc
       (arith (+ $i 1) $stop (+ $acc (* $i $i)))))

; one branch = one independent arithmetic loop of SPAN (=100000) steps,
; offset by $base so every branch is a distinct computation.
(= (branch-A $base) (arith $base (+ $base 100000) 0))

; EDIT THIS to 1 / 2 / 4 / 8 for the scaling ladder.
!(pragma! num-threads 8)

; 8 independent branches, distinct bases (defeats memoization).
!(collapse
   (hyperpose
     ((branch-A 0)
      (branch-A 1000000)
      (branch-A 2000000)
      (branch-A 3000000)
      (branch-A 4000000)
      (branch-A 5000000)
      (branch-A 6000000)
      (branch-A 7000000))))
