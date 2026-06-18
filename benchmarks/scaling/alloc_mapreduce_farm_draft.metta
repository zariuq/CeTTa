; ===========================================================================
; DRAFT  --  control workload (B): ALLOCATION-heavy map/reduce farm
; ---------------------------------------------------------------------------
; STATUS: DRAFT for the hyperpose threaded-scaling investigation. Sizes are a
; first cut and MUST be calibrated on a QUIET machine before any headline
; numbers. Pairs with  ep_arith_farm_draft.metta  (workload A).
;
; PURPOSE: the HIGH-allocation arm of the control pair. Same OUTER loop shape
; as (A) -- an N-step loop with distinct args -- but each step BUILDS a fresh
; K-length Cons-list and FOLDS it with a pattern-matched recursion. So per
; outer step it does ~(1 + 2K) match calls and allocates K cons cells, i.e.
; ~40x the match/heap traffic of (A) for the same outer N. This maximizes the
; per-call heap churn that we suspect is hitting glibc's arena lock.
;
; READING THE RESULT (run BOTH files through the same 1->8T ladder):
;   * (A) scales ~linearly to 8T but THIS file (B) does not -> the bottleneck
;     is the per-call match/bindings heap traffic (allocator-lock contention),
;     and the fix is to route those buffers through the per-branch arena /
;     thread-local pools instead of malloc.
;   * BOTH scale poorly -> the threading substrate itself is implicated.
;
; DESIGN NOTES (verified against this engine on 2026-06-07):
;   * No TCO; depth is a ~2-units/level logical limit. The OUTER loop depth is
;     N, and each step transiently adds build+fold depth ~2K. Keep N + 2K well
;     under the depth budget: at max-stack-depth 400000, N=80000 with K=20 is
;     safe (peak ~80040). Raising N or K needs a bigger max-stack-depth.
;   * `build` is seeded by the loop index ($i) so each list is DISTINCT ->
;     memoization cannot short-circuit the allocation.
;   * Each branch uses a distinct base offset (same reason).
;   * `collapse` forces all branches to run fully (steady-state throughput).
;
; HOW TO RUN: identical ladder to workload A -- compare Elapsed AND
;   "Voluntary context switches" at 1 / 2 / 4 / 8 threads on a quiet machine.
;
; CALIBRATION KNOBS:
;   * N (outer steps) and K (list length per step). Keep N + 2K under the depth
;     budget. Increase K to push allocation intensity; increase N (with a
;     larger max-stack-depth) for a longer run.
; ===========================================================================

!(pragma! max-stack-depth 400000)

; build the Cons-list [seed, seed+1, ..., seed+k-1] (tail-recursive on $acc).
(= (build $k $seed $acc)
   (if (== $k 0)
       $acc
       (build (- $k 1) (+ $seed 1) (Cons $seed $acc))))

; fold-sum a Cons-list via pattern match (this is the heap-churny match shape).
(= (sum-list Nil) 0)
(= (sum-list (Cons $h $t)) (+ $h (sum-list $t)))

; outer loop: N steps; each step builds a fresh K-list seeded by the index $i
; (distinct -> no memo) and folds it into the accumulator.
(= (alloc-loop $i $stop $k $acc)
   (if (>= $i $stop)
       $acc
       (alloc-loop (+ $i 1) $stop $k (+ $acc (sum-list (build $k $i Nil))))))

; one branch = N (=80000) outer steps, K (=20) list length, offset by $base.
(= (branch-B $base) (alloc-loop $base (+ $base 80000) 20 0))

; EDIT THIS to 1 / 2 / 4 / 8 for the scaling ladder.
!(pragma! num-threads 8)

; 8 independent branches, distinct bases (defeats memoization).
!(collapse
   (hyperpose
     ((branch-B 0)
      (branch-B 1000000)
      (branch-B 2000000)
      (branch-B 3000000)
      (branch-B 4000000)
      (branch-B 5000000)
      (branch-B 6000000)
      (branch-B 7000000))))
