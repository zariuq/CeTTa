(set:profile megalodon-hotg)

; Native proof terms.
;
; A proof is ordinary data: a `pf:` tree.  `set:theorem` checks it against a
; proposition and records it; `set:native-proof` compiles the retained tree to
; a dependent kernel term and replays it in the regular kernel, giving a
; package; `set:native-use` and `set:native-normalize` apply a consumer to the
; exact checked package.  A package is authority only if it agrees with a
; fresh compilation of an admitted proof: forged packages are refused.

!(add-atom &self (: A prop))
!(add-atom &self (: B prop))

; 1-2. A closed proof of (all p. p -> p), and the kernel term it compiles to.
!(set:theorem id-proof
   (all prop (lam p (imp p p)))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof id-proof)
   (Compiled $name $term (size-atom $assumptions)))

; 3-4. Proofs are data a program builds.  One tree for A -> (B -> A), with
;      the hypothesis index as a parameter: index 1 proves it, index 0 is
;      refuted with the mismatch the checker found.
(= (k-proof $i) (pf:imp-intro (pf:imp-intro (pf:hyp $i))))
!(let $p (k-proof 1) (try (set:proves $p (imp A (imp B A)))))
!(let $p (k-proof 0) (try (set:proves $p (imp A (imp B A)))))

; 5. The checked program-built proof is recorded, then compiled.
!(let $p (k-proof 1) (set:theorem k-proof-1 (imp A (imp B A)) $p))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof k-proof-1)
   (Compiled $name $term))

; 6. A dependent consumer of the exact package: reflexivity at the compiled
;    proof, whose result type is the identity type of that proof.
!(let $package (set:native-proof id-proof)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (SetNativeUseV1 $checked $consumer $result $result-type)
          (set:native-use $package (Lam $type (Refl (idx 0))))
       (let (Id $carrier $left $right) $result-type
         (Use (== $checked $package) (== $left $term) (== $right $term))))))

; 7. Normalization of a consumer application.
!(let $package (set:native-proof k-proof-1)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (SetNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
          (set:native-normalize $package (Lam $type (Refl (idx 0))))
       (Normal (== $normal (Refl $term))))))

; 8. A consumer with the wrong domain is refuted.
!(let $package (set:native-proof id-proof)
   (let $result (try (set:native-use $package (Lam (Sort (LevelConst 0)) (Refl (idx 0)))))
     (car-atom $result)))

; 9-10. Package-shaped syntax is not authority: a changed term is refuted, a
;       foreign signature digest abstains.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof id-proof)
   (let $forged (SetNativeProofV1 $name $prop $proof (Refl $term) $type
                  $context $rules $assumptions $digest)
     (car-atom (try (set:native-use $forged (Lam $type (Refl (idx 0))))))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof id-proof)
   (let $foreign (SetNativeProofV1 $name $prop $proof $term $type
                   $context $rules $assumptions "foreign-signature")
     (car-atom (try (set:native-use $foreign (Lam $type (Refl (idx 0))))))))

; 11-12. A cited axiom stays an assumption of the package; it is never
;        laundered into a closed proof.
!(set:axiom premise (In Empty (Power Empty)))
!(set:theorem from-premise (In Empty (Power Empty)) (pf:known premise))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof from-premise)
   (Assumptions (size-atom $assumptions)))

; 13. A pf:by step counts as evidence only through the eliminations it stands
;     for: the package is compiled from them and carries them as its proof.
!(set:theorem by-name (In Empty (UnivOf Empty)) (pf:by universeIn))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof by-name)
   (Compiled $proof))
