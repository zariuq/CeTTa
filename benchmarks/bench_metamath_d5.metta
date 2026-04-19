; Metamath proof search: depth 5, 2 axioms (a1: right-Euclidean, a2: zero-identity).
; Dialect-agnostic — runs identically on CeTTa, PeTTa, and HE.
; Produces 85,013 proof trees of (⟨=⟩ $t $t).
;
; Timings: PeTTa ~32s (WINS), CeTTa ~94s, HE timeout (>120s).
; Source: nilbc.metta Metamath demo0 easy KB.
; Change fromNumber 5 → fromNumber 3 for a quick run (~0.2s).
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

; Medium KB from nilbc.metta (Metamath axioms)
!(bind! &kbm (new-space))
!(add-atom &kbm (: a1 (-> (: $ter (⟨=⟩ $t $r))
                           (: $tes (⟨=⟩ $t $s))
                           (⟨=⟩ $r $s))))
!(add-atom &kbm (: a2 (⟨=⟩ (⟨+⟩ $t ⟨0⟩) $t)))

; Try depth 4 — should produce more proof trees than depth 3
!(bc &kbm (fromNumber 5) (: $prf (⟨=⟩ $t $t)))
