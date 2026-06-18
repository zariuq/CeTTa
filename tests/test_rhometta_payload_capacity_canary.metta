; ============================================================================
; RHOMETTA PAYLOAD CAPACITY CANARY  (B1 — fixed-64 map caps)
; ============================================================================
;
; WHAT THIS GUARDS
;   The rhometta deferred-payload evaluator builds four per-payload maps that
;   were authored as fixed 64-element arrays with a hard `return false` at the
;   65th entry (NO grow):
;     RHO_OVERLAY_MAP_CAP        src/rhocalc_core.c:1338   (spaces reached)
;     RHO_STATE_CLONE_MAP_CAP    src/rhocalc_core.c:1345   (state cells reached)
;     EVAL_PAYLOAD_SPACE_REDIRECT_CAP   src/eval.c:53      (escaping space redirects)
;     EVAL_PAYLOAD_STATE_REDIRECT_CAP   src/eval.c:60      (escaping state redirects)
;   A plain (non-adversarial) payload touching >64 distinct spaces / state cells
;   therefore HARD-ERRORS ("rhometta reduction failed") — or, in branchy/
;   superpose payloads, SILENTLY drops the over-cap results.  This violates the
;   "no stupid small-number limits" intent.  Confirmed live on the engine
;   before the fix (2026-06-18): SC-64 ok / SC-65 error; OV-63 ok / OV-64 error.
;
; STATUS:  GREEN only after the B1 dynamic-map fix. This is manifest-wired so
;          fixed small-number payload-map limits cannot return quietly.
;          See the fixed-cap follow-up in
;          docs/plans/2026-06-11_rhometta_event_semantics_two_lane_todo.txt.
; ============================================================================

!(import! &self rhometta)
!(import! &self ../lib/lib_pln)

; N distinct already-evaluated state cells / spaces, baked into a payload term.
; (range-atom MUST be evaluated before superpose, hence the let-bind of $r.)
(= (cells $n)  (let $r (range-atom 1 $n) (collapse (let $i (superpose $r) (new-state $i)))))
(= (spaces $n) (let $r (range-atom 1 $n) (collapse (let $i (superpose $r) (new-space)))))

; A payload whose quoted term embeds the N literals (so the collection walk
; counts them) and whose body just yields `done`.
(= (sc $n)
   (let $c (cells $n)
     (rho:par (rho:send (rho:quote rho:nil) (rhometta:eval (let $u $c done)))
              (rho:recv (rho:quote rho:nil) $x (rho:val $x)))))
(= (ov $n)
   (let $c (spaces $n)
     (rho:par (rho:send (rho:quote rho:nil) (rhometta:eval (let $u $c done)))
              (rho:recv (rho:quote rho:nil) $x (rho:val $x)))))

; Success shape (validated on the engine): one quiescent outcome carrying `done`.
; A cap trip instead yields ((Error (__cetta_lib_rhometta_run ...) "rhometta reduction failed")).

; -- SANITY (under cap; PASSES today and after the fix) ---------------------
!(assertEqualToResult (collapse (rhometta:run (sc 65))) (((rho:val (rho:quote (rho:val done))))))  ; 64 state cells
!(assertEqualToResult (collapse (rhometta:run (ov 64))) (((rho:val (rho:quote (rho:val done))))))  ; 63 payload spaces (+self=64)

; -- B1 CANARY (just over the former cap) -----------------------------------
; These two cases drive both layers:
;   sc 66 -> 65 collected state cells, then 65 eval state redirects
;   ov 65 -> 64 collected payload spaces + &self, then 65 eval space redirects
!(assertEqualToResult (collapse (rhometta:run (sc 66))) (((rho:val (rho:quote (rho:val done))))))
!(assertEqualToResult (collapse (rhometta:run (ov 65))) (((rho:val (rho:quote (rho:val done))))))

; -- REDIRECT CANARY (branchy writes to 65 registry-bound spaces) -----------
; This is the surface-level redirect test: the registry must keep all 65 bound
; spaces, the payload must be able to create >64 per-payload space redirects,
; and sibling isolation must still prevent those branchy writes from escaping.
!(bind! &cap-space-01 (new-space))
!(bind! &cap-space-02 (new-space))
!(bind! &cap-space-03 (new-space))
!(bind! &cap-space-04 (new-space))
!(bind! &cap-space-05 (new-space))
!(bind! &cap-space-06 (new-space))
!(bind! &cap-space-07 (new-space))
!(bind! &cap-space-08 (new-space))
!(bind! &cap-space-09 (new-space))
!(bind! &cap-space-10 (new-space))
!(bind! &cap-space-11 (new-space))
!(bind! &cap-space-12 (new-space))
!(bind! &cap-space-13 (new-space))
!(bind! &cap-space-14 (new-space))
!(bind! &cap-space-15 (new-space))
!(bind! &cap-space-16 (new-space))
!(bind! &cap-space-17 (new-space))
!(bind! &cap-space-18 (new-space))
!(bind! &cap-space-19 (new-space))
!(bind! &cap-space-20 (new-space))
!(bind! &cap-space-21 (new-space))
!(bind! &cap-space-22 (new-space))
!(bind! &cap-space-23 (new-space))
!(bind! &cap-space-24 (new-space))
!(bind! &cap-space-25 (new-space))
!(bind! &cap-space-26 (new-space))
!(bind! &cap-space-27 (new-space))
!(bind! &cap-space-28 (new-space))
!(bind! &cap-space-29 (new-space))
!(bind! &cap-space-30 (new-space))
!(bind! &cap-space-31 (new-space))
!(bind! &cap-space-32 (new-space))
!(bind! &cap-space-33 (new-space))
!(bind! &cap-space-34 (new-space))
!(bind! &cap-space-35 (new-space))
!(bind! &cap-space-36 (new-space))
!(bind! &cap-space-37 (new-space))
!(bind! &cap-space-38 (new-space))
!(bind! &cap-space-39 (new-space))
!(bind! &cap-space-40 (new-space))
!(bind! &cap-space-41 (new-space))
!(bind! &cap-space-42 (new-space))
!(bind! &cap-space-43 (new-space))
!(bind! &cap-space-44 (new-space))
!(bind! &cap-space-45 (new-space))
!(bind! &cap-space-46 (new-space))
!(bind! &cap-space-47 (new-space))
!(bind! &cap-space-48 (new-space))
!(bind! &cap-space-49 (new-space))
!(bind! &cap-space-50 (new-space))
!(bind! &cap-space-51 (new-space))
!(bind! &cap-space-52 (new-space))
!(bind! &cap-space-53 (new-space))
!(bind! &cap-space-54 (new-space))
!(bind! &cap-space-55 (new-space))
!(bind! &cap-space-56 (new-space))
!(bind! &cap-space-57 (new-space))
!(bind! &cap-space-58 (new-space))
!(bind! &cap-space-59 (new-space))
!(bind! &cap-space-60 (new-space))
!(bind! &cap-space-61 (new-space))
!(bind! &cap-space-62 (new-space))
!(bind! &cap-space-63 (new-space))
!(bind! &cap-space-64 (new-space))
!(bind! &cap-space-65 (new-space))

(= (touch-bound-spaces)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $u (collapse
                   (superpose
                     ((add-atom &cap-space-01 (hit 01))
                      (add-atom &cap-space-02 (hit 02))
                      (add-atom &cap-space-03 (hit 03))
                      (add-atom &cap-space-04 (hit 04))
                      (add-atom &cap-space-05 (hit 05))
                      (add-atom &cap-space-06 (hit 06))
                      (add-atom &cap-space-07 (hit 07))
                      (add-atom &cap-space-08 (hit 08))
                      (add-atom &cap-space-09 (hit 09))
                      (add-atom &cap-space-10 (hit 10))
                      (add-atom &cap-space-11 (hit 11))
                      (add-atom &cap-space-12 (hit 12))
                      (add-atom &cap-space-13 (hit 13))
                      (add-atom &cap-space-14 (hit 14))
                      (add-atom &cap-space-15 (hit 15))
                      (add-atom &cap-space-16 (hit 16))
                      (add-atom &cap-space-17 (hit 17))
                      (add-atom &cap-space-18 (hit 18))
                      (add-atom &cap-space-19 (hit 19))
                      (add-atom &cap-space-20 (hit 20))
                      (add-atom &cap-space-21 (hit 21))
                      (add-atom &cap-space-22 (hit 22))
                      (add-atom &cap-space-23 (hit 23))
                      (add-atom &cap-space-24 (hit 24))
                      (add-atom &cap-space-25 (hit 25))
                      (add-atom &cap-space-26 (hit 26))
                      (add-atom &cap-space-27 (hit 27))
                      (add-atom &cap-space-28 (hit 28))
                      (add-atom &cap-space-29 (hit 29))
                      (add-atom &cap-space-30 (hit 30))
                      (add-atom &cap-space-31 (hit 31))
                      (add-atom &cap-space-32 (hit 32))
                      (add-atom &cap-space-33 (hit 33))
                      (add-atom &cap-space-34 (hit 34))
                      (add-atom &cap-space-35 (hit 35))
                      (add-atom &cap-space-36 (hit 36))
                      (add-atom &cap-space-37 (hit 37))
                      (add-atom &cap-space-38 (hit 38))
                      (add-atom &cap-space-39 (hit 39))
                      (add-atom &cap-space-40 (hit 40))
                      (add-atom &cap-space-41 (hit 41))
                      (add-atom &cap-space-42 (hit 42))
                      (add-atom &cap-space-43 (hit 43))
                      (add-atom &cap-space-44 (hit 44))
                      (add-atom &cap-space-45 (hit 45))
                      (add-atom &cap-space-46 (hit 46))
                      (add-atom &cap-space-47 (hit 47))
                      (add-atom &cap-space-48 (hit 48))
                      (add-atom &cap-space-49 (hit 49))
                      (add-atom &cap-space-50 (hit 50))
                      (add-atom &cap-space-51 (hit 51))
                      (add-atom &cap-space-52 (hit 52))
                      (add-atom &cap-space-53 (hit 53))
                      (add-atom &cap-space-54 (hit 54))
                      (add-atom &cap-space-55 (hit 55))
                      (add-atom &cap-space-56 (hit 56))
                      (add-atom &cap-space-57 (hit 57))
                      (add-atom &cap-space-58 (hit 58))
                      (add-atom &cap-space-59 (hit 59))
                      (add-atom &cap-space-60 (hit 60))
                      (add-atom &cap-space-61 (hit 61))
                      (add-atom &cap-space-62 (hit 62))
                      (add-atom &cap-space-63 (hit 63))
                      (add-atom &cap-space-64 (hit 64))
                      (add-atom &cap-space-65 (hit 65)))))
           done)))
     (rho:recv (rho:quote rho:nil) $x (rho:val $x))))

!(assertEqualToResult (collapse (rhometta:run (touch-bound-spaces))) (((rho:val (rho:quote (rho:val done))))))
!(assertEqual
  (TupleCount
    (collapse
      (superpose
        ((match &cap-space-01 (hit $x) $x)
         (match &cap-space-02 (hit $x) $x)
         (match &cap-space-03 (hit $x) $x)
         (match &cap-space-04 (hit $x) $x)
         (match &cap-space-05 (hit $x) $x)
         (match &cap-space-06 (hit $x) $x)
         (match &cap-space-07 (hit $x) $x)
         (match &cap-space-08 (hit $x) $x)
         (match &cap-space-09 (hit $x) $x)
         (match &cap-space-10 (hit $x) $x)
         (match &cap-space-11 (hit $x) $x)
         (match &cap-space-12 (hit $x) $x)
         (match &cap-space-13 (hit $x) $x)
         (match &cap-space-14 (hit $x) $x)
         (match &cap-space-15 (hit $x) $x)
         (match &cap-space-16 (hit $x) $x)
         (match &cap-space-17 (hit $x) $x)
         (match &cap-space-18 (hit $x) $x)
         (match &cap-space-19 (hit $x) $x)
         (match &cap-space-20 (hit $x) $x)
         (match &cap-space-21 (hit $x) $x)
         (match &cap-space-22 (hit $x) $x)
         (match &cap-space-23 (hit $x) $x)
         (match &cap-space-24 (hit $x) $x)
         (match &cap-space-25 (hit $x) $x)
         (match &cap-space-26 (hit $x) $x)
         (match &cap-space-27 (hit $x) $x)
         (match &cap-space-28 (hit $x) $x)
         (match &cap-space-29 (hit $x) $x)
         (match &cap-space-30 (hit $x) $x)
         (match &cap-space-31 (hit $x) $x)
         (match &cap-space-32 (hit $x) $x)
         (match &cap-space-33 (hit $x) $x)
         (match &cap-space-34 (hit $x) $x)
         (match &cap-space-35 (hit $x) $x)
         (match &cap-space-36 (hit $x) $x)
         (match &cap-space-37 (hit $x) $x)
         (match &cap-space-38 (hit $x) $x)
         (match &cap-space-39 (hit $x) $x)
         (match &cap-space-40 (hit $x) $x)
         (match &cap-space-41 (hit $x) $x)
         (match &cap-space-42 (hit $x) $x)
         (match &cap-space-43 (hit $x) $x)
         (match &cap-space-44 (hit $x) $x)
         (match &cap-space-45 (hit $x) $x)
         (match &cap-space-46 (hit $x) $x)
         (match &cap-space-47 (hit $x) $x)
         (match &cap-space-48 (hit $x) $x)
         (match &cap-space-49 (hit $x) $x)
         (match &cap-space-50 (hit $x) $x)
         (match &cap-space-51 (hit $x) $x)
         (match &cap-space-52 (hit $x) $x)
         (match &cap-space-53 (hit $x) $x)
         (match &cap-space-54 (hit $x) $x)
         (match &cap-space-55 (hit $x) $x)
         (match &cap-space-56 (hit $x) $x)
         (match &cap-space-57 (hit $x) $x)
         (match &cap-space-58 (hit $x) $x)
         (match &cap-space-59 (hit $x) $x)
         (match &cap-space-60 (hit $x) $x)
         (match &cap-space-61 (hit $x) $x)
         (match &cap-space-62 (hit $x) $x)
         (match &cap-space-63 (hit $x) $x)
         (match &cap-space-64 (hit $x) $x)
         (match &cap-space-65 (hit $x) $x)))))
  0)

; -- REDIRECT CANARY (branchy writes to 65 state cells) ---------------------
; This is the state-cell companion to the bound-space test above.  The payload
; branches over 65 state cells and changes every one of them, forcing >64 state
; redirects.  After the run, sibling isolation requires the original states to
; retain their initial values, so the count of escaped `changed` cells is zero.
!(bind! &cap-states (cells 66))

(= (touch-bound-states)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $u (collapse
                   (let $s (superpose &cap-states)
                     (change-state! $s changed)))
           done)))
     (rho:recv (rho:quote rho:nil) $x (rho:val $x))))

!(assertEqualToResult (collapse (rhometta:run (touch-bound-states))) (((rho:val (rho:quote (rho:val done))))))
!(assertEqual
  (TupleCount
    (collapse
      (let $s (superpose &cap-states)
        (if (== (get-state $s) changed) changed Empty))))
  0)
