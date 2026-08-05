; PROBE (2026-08-05, Fable): the one real remaining PathMap scan class.
; Interleaved mutation+query forces a FULL-STORE shadow refresh on every
; dirty->read transition.  Measured on the runtime-stats binary
; (--space-engine pathmap --emit-runtime-stats), 2000 atoms + 100 queries:
;   rules+facts in &self : pathmap-materialize-native = 2002 events /
;                          2,461,459 atoms  (quadratic; normal binary 3.84s
;                          vs native engine 0.06s = 64x at this scale;
;                          20k atoms exceeds 180s)
;   facts in own space   : 3 events / 2,459 atoms (0.09s) - phase-amortized
; imported-bridge-v2/v3 hit = 0 in ALL runs, including with
; CETTA_PATHMAP_QUERY_INDEX=1: the indexed multiway cursors never engage for
; standard match shapes - routing/admission gap, not a missing engine.
; The justified fix is an order-respecting delta shadow sync (or in-bridge
; exact-row serving); see probe_enumeration_order_contract.metta for the
; order law any delta protocol must preserve.  Not in the manifest: this is
; the discriminator for that follow-on, to be promoted when it lands.
(= (load $n)
   (if (== $n 0)
       done
       (let $ig (add-atom &self (edge $n $n))
            (load (- $n 1)))))
!(load 2000)
(= (qloop $n $acc)
   (if (== $n 0)
       $acc
       (qloop (- $n 1) (+ $acc (match &self (edge 500 $x) 1)))))
!(qloop 100 0)
; expected: [done] then [100] on every lane/engine
