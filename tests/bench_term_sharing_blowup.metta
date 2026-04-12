; =============================================================================
; Term Sharing Blowup Test
; =============================================================================
;
; Backward chaining benchmark that still stresses variable-bearing proof terms.
;
; Phase 1 exact sharing helps common immutable structure but does not fully
; solve the variable-bearing explosion. This witness is meant to stay around
; as the Layer 3 / Layer 4 yardstick.
;
; Usage:
;   ulimit -v 2097152 && timeout 30 ./cetta tests/bench_term_sharing_blowup.metta
;
; Quick validation:
;   Change (blowup-depth) to 3 below.
;
; =============================================================================

; === CONFIGURATION ===
; Depth 3 = safe, Depth 4 = hard witness
(= (blowup-depth) 4)

; =============================================================================
; Backward Chainer (nilbc-style with proof terms)
; =============================================================================

(: Nat Type) (: Z Nat) (: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(= (bc $kb $_ (: $proof $theorem))
   (match $kb (: $proof $theorem) (: $proof $theorem)))

(= (bc $kb (S $depth) (: ($rule $p1 $p2) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) (: $p2 $t2) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) (: $p2 $t2) $theorem))))
          ((: $p1 $t1) (bc $kb $depth (: $p1 $t1)))
          ((: $p2 $t2) (bc $kb $depth (: $p2 $t2))))
     (: ($rule $p1 $p2) $theorem)))

(= (bc $kb (S $depth) (: ($rule $p1) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) $theorem))))
          ((: $p1 $t1) (bc $kb $depth (: $p1 $t1))))
     (: ($rule $p1) $theorem)))

; =============================================================================
; Knowledge Base (equality axioms)
; =============================================================================

!(bind! &kb (new-space))

!(add-atom &kb (: a1 (-> (: $ter (eq $t $r))
                          (: $tes (eq $t $s))
                          (eq $r $s))))
!(add-atom &kb (: a2 (eq (plus $t zero) $t)))
!(add-atom &kb (: a3 (-> (: $p (eq $x $y)) (eq $y $x))))
!(add-atom &kb (: a4 (-> (: $p (eq $x $y))
                          (: $q (eq $y $z))
                          (eq $x $z))))

!(add-atom &kb (: b1 (eq zero zero)))
!(add-atom &kb (: b2 (eq one one)))
!(add-atom &kb (: b3 (eq (plus zero zero) zero)))

; =============================================================================
; Run Test
; =============================================================================

!(println! "=== Term Sharing Blowup Test ===")
!(println! "")
!(println! "Phase 1 exact sharing may still time out here.")
!(println! "Layer 3 / Layer 4 should make this witness materially better.")
!(println! "")

!(bind! &results (collapse (bc &kb (fromNumber (blowup-depth)) (: $prf (eq $t $t)))))

!(println! "=== SUCCESS: Blowup avoided! ===")
!(println! "Proof count:")
!(println! (size-atom &results))
