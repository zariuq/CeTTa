; PeTTa backward-chaining benchmark trimmed from nilbc's easy lane.
; Final foldall count should be 1.

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(: bc (-> $a Nat $b $b))

(= (bc $knowledge_base $_ (: $proof $theorem))
   (match $knowledge_base (: $proof $theorem) (: $proof $theorem)))

(= (bc $knowledge_base (S $depth) (: ($proof_rule $premise_proof1 $premise_proof2) $theorem))
   (let* (((: $proof_rule
             (-> (: $premise_proof1 $premise_type1)
                 (: $premise_proof2 $premise_type2)
                 $theorem))
            (bc $knowledge_base $depth
                (: $proof_rule
                   (-> (: $premise_proof1 $premise_type1)
                       (: $premise_proof2 $premise_type2)
                       $theorem))))
          ((: $premise_proof1 $premise_type1)
           (bc $knowledge_base $depth (: $premise_proof1 $premise_type1)))
          ((: $premise_proof2 $premise_type2)
           (bc $knowledge_base $depth (: $premise_proof2 $premise_type2))))
     (: ($proof_rule $premise_proof1 $premise_proof2) $theorem)))

!(bind! &kbe (new-space))

!(add-atom &kbe (: a1 (-> (: $ter (⟨=⟩ $t $r))
                          (: $tes (⟨=⟩ $t $s))
                          (⟨=⟩ $r $s))))

!(add-atom &kbe (: a2 (⟨=⟩ (⟨+⟩ $t ⟨0⟩) $t)))

(= (merge $a $b) (+ $a $b))
(= (proof-item)
   (let $r (bc &kbe (fromNumber 1) (: $prf (⟨=⟩ $t $t))) 1))

!(foldall merge (proof-item) 0)
