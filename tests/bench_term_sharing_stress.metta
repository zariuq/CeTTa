; =============================================================================
; Term Sharing Stress Test
; =============================================================================
;
; Targeted benchmark for term-sharing implementation.
; Tests sharing invariants in isolation rather than as a side effect of a
; larger workload.
;
; Usage:
;   ./cetta --emit-runtime-stats tests/bench_term_sharing_stress.metta
;
; =============================================================================

; --- Configuration ---
(= (stress-N) 1000)

; =============================================================================
; Test 1: Deep Term Duplication
; =============================================================================

!(println! "=== Test 1: Deep Term Duplication ===")

!(bind! &dedup-space (new-space))

(= (add-n-times $space $term $n)
   (if (<= $n 0)
       done
       (let () (add-atom-nodup $space $term)
         (add-n-times $space $term (- $n 1)))))

!(reset-runtime-stats!)

!(add-n-times &dedup-space
              (nested (a (b (c (d (e (f (g (h (i j))))))))))
              (stress-N))

!(bind! &stats1 (runtime-stats!))

!(println! (space-len &dedup-space))
!(println! (match &stats1 (runtime-counter hashcons-hit $n) (hashcons-hit $n)))
!(println! (match &stats1 (runtime-counter hashcons-attempt $n) (hashcons-attempt $n)))
!(assertEqual (space-len &dedup-space) 1)

; =============================================================================
; Test 2: Multi-Space Sharing
; =============================================================================

!(println! "=== Test 2: Multi-Space Sharing ===")

!(bind! &space-a (new-space))
!(bind! &space-b (new-space))

!(reset-runtime-stats!)
!(add-atom &space-a (shared-term alpha beta gamma delta))
!(add-atom &space-b (shared-term alpha beta gamma delta))
!(bind! &stats2 (runtime-stats!))

!(println! (match &stats2 (runtime-counter hashcons-hit $n) (hashcons-hit $n)))
!(println! (match &stats2 (runtime-counter hashcons-attempt $n) (hashcons-attempt $n)))

; =============================================================================
; Test 3: Pattern Instantiation Sharing
; =============================================================================

!(println! "=== Test 3: Pattern Instantiation Sharing ===")

!(bind! &facts (new-space))
!(bind! &wrapped (new-space))

(= (add-facts-loop $n)
   (if (<= $n 0)
       done
       (let () (add-atom &facts (item $n))
         (add-facts-loop (- $n 1)))))

!(add-facts-loop 100)

!(reset-runtime-stats!)

!(let $results (collapse (match &facts (item $x) (wrapped (item $x))))
   (add-atom &wrapped $results))

!(bind! &stats3 (runtime-stats!))

!(println! (match &stats3 (runtime-counter hashcons-hit $n) (hashcons-hit $n)))
!(println! (match &stats3 (runtime-counter hashcons-attempt $n) (hashcons-attempt $n)))

; =============================================================================
; Test 4: Recursive Production Sharing
; =============================================================================

!(println! "=== Test 4: Recursive Production Sharing ===")

(= (produce-same $n)
   (if (<= $n 0)
       ()
       (let $rest (produce-same (- $n 1))
         (cons-atom (constant-term x y z) $rest))))

!(reset-runtime-stats!)
!(bind! &produced (produce-same 100))
!(bind! &stats4 (runtime-stats!))

!(println! (match &stats4 (runtime-counter hashcons-hit $n) (hashcons-hit $n)))
!(println! (match &stats4 (runtime-counter hashcons-attempt $n) (hashcons-attempt $n)))

; =============================================================================
; Test 5: Ground Value Sharing
; =============================================================================

!(println! "=== Test 5: Ground Value Sharing ===")

!(bind! &ground-space (new-space))

(= (add-ground-n-times $space $val $n)
   (if (<= $n 0)
       done
       (let () (add-atom-nodup $space (ground-wrap $val))
         (add-ground-n-times $space $val (- $n 1)))))

!(reset-runtime-stats!)
!(add-ground-n-times &ground-space 42 100)
!(bind! &stats5 (runtime-stats!))

!(println! (space-len &ground-space))
!(println! (match &stats5 (runtime-counter hashcons-hit $n) (hashcons-hit $n)))
!(assertEqual (space-len &ground-space) 1)

; =============================================================================
; Test 6: Layer 3 Placeholder
; =============================================================================
; Alpha-equivalent variable sharing is expected to stay near zero until a
; dedicated Layer 3 key / indexing design exists.

!(println! "=== Test 6: Alpha Canonical Variable Sharing ===")

!(bind! &alpha-a (new-space))
!(bind! &alpha-b (new-space))

!(reset-runtime-stats!)
!(add-atom &alpha-a (alpha-pair $x $x))
!(add-atom &alpha-b (alpha-pair $y $y))
!(bind! &stats6 (runtime-stats!))

!(println! (match &stats6 (runtime-counter term-universe-lookup $n)
                     (term-universe-lookup $n)))
!(println! (match &stats6 (runtime-counter term-universe-hit $n)
                     (term-universe-hit $n)))
!(println! (match &stats6 (runtime-counter term-universe-insert $n)
                     (term-universe-insert $n)))

; =============================================================================
; Summary
; =============================================================================

!(println! "=== Stress Test Complete ===")
!(println! "")
!(println! "CURRENT BASELINE (Phase 1 exact sharing):")
!(println! "- Test 1: hashcons-hit should be high")
!(println! "- Test 2: cross-space sharing should reuse exact structure")
!(println! "- Test 3: wrapper structure should begin sharing")
!(println! "- Test 4: repeated production should show meaningful hashcons activity")
!(println! "- Test 5: ground value sharing should dedup repeated values")
!(println! "- Test 6: alpha-equivalent var sharing is still a Layer 3 target")
!(println! "")
!(println! "LAYER 3 TARGET:")
!(println! "- term-universe-hit should become >0 for alpha-equivalent vars")
