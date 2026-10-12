(hol:profile megalodon-hotg)

; The four UnivOf axioms of Megalodon's ramsey36/preamble.mgs and their
; supporting definitions. Here they are theorems: each is one of the four
; universe laws of the signature, up to the definitions and the grouping of
; the closure conditions. This tests the universe fragment, not admission of
; the complete HOTG preamble.
!(hol:define and (-> prop prop prop)
   (= (and $A $B) (all prop (lam p (imp (imp $A (imp $B p)) p)))))
!(hol:define Subq (-> set set prop)
   (= (Subq $A $B) (all set (lam x (imp (In x $A) (In x $B))))))
!(hol:define TransSet (-> set prop)
   (= (TransSet $U) (all set (lam x (imp (In x $U) (Subq x $U))))))
!(hol:define Union_closed (-> set prop)
   (= (Union_closed $U) (all set (lam X (imp (In X $U) (In (Union X) $U))))))
!(hol:define Power_closed (-> set prop)
   (= (Power_closed $U) (all set (lam X (imp (In X $U) (In (Power X) $U))))))
!(hol:define Repl_closed (-> set prop)
   (= (Repl_closed $U)
      (all set (lam X (imp (In X $U)
        (all (-> set set) (lam F (imp
          (all set (lam x (imp (In x X) (In (F x) $U))))
          (In (Repl X F) $U)))))))))
!(hol:define ZF_closed (-> set prop)
   (= (ZF_closed $U)
      (and (and (Union_closed $U) (Power_closed $U)) (Repl_closed $U))))
!(hol:theorem UnivOf_In (all set (lam N (In N (UnivOf N)))) (pf:known universeIn))
!(hol:theorem UnivOf_TransSet (all set (lam N (TransSet (UnivOf N)))) (pf:known universeTransitive))
; ZF_closed groups the three closures as (Union and Power) and Repl, the law
; as Union and (Power and Repl): the proof takes one apart and builds the other.
!(hol:theorem UnivOf_ZF_closed (all set (lam N (ZF_closed (UnivOf N))))
   (pf:all-intro (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeClosed) (pf:var 2)) (Union_closed (UnivOf (pf:var 2)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1))))) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeClosed) (pf:var 2)) (and (Power_closed (UnivOf (pf:var 2))) (Repl_closed (UnivOf (pf:var 2))))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))) (Power_closed (UnivOf (pf:var 2)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1)))))))) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeClosed) (pf:var 1)) (and (Power_closed (UnivOf (pf:var 1))) (Repl_closed (UnivOf (pf:var 1))))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))) (Repl_closed (UnivOf (pf:var 1)))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))))))))
!(hol:theorem UnivOf_Min
   (all set (lam N (all set (lam U
     (imp (In N U) (imp (TransSet U) (imp (ZF_closed U) (Subq (UnivOf N) U))))))))
   (pf:all-intro (pf:all-intro (pf:imp-intro (pf:imp-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeMinimal) (pf:var 1)) (pf:var 0)) (pf:hyp 2)) (pf:hyp 1)) (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:hyp 1) (and (Union_closed (pf:var 1)) (Power_closed (pf:var 1)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1)))) (Union_closed (pf:var 1))) (pf:imp-intro (pf:imp-intro (pf:hyp 1))))) (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:hyp 2) (and (Union_closed (pf:var 2)) (Power_closed (pf:var 2)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1)))) (Power_closed (pf:var 2))) (pf:imp-intro (pf:imp-intro (pf:hyp 0))))) (pf:imp-elim (pf:all-elim (pf:hyp 2) (Repl_closed (pf:var 2))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))))))))))))))))

; All four have native proof-family representations. Each package retains
; exactly the universe law it is proved from as its one assumption.
!(map-atom (UnivOf_In UnivOf_TransSet UnivOf_ZF_closed UnivOf_Min) $law
   (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        (hol:native-proof $law)
     (UniverseLaw $name (size-atom $assumptions))))

; Eliminate both conjunctions in ZF_closed to obtain Power_closed. The
; retained tree contains only the same implication/quantifier proof rules
; used by the native compiler, rather than a new trusted projection rule.
!(hol:theorem universe-power-closure
   (all set (lam N (Power_closed (UnivOf N))))
   (pf:all-intro
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known UnivOf_ZF_closed) (pf:var 0))
         (Power_closed (UnivOf (pf:var 0))))
       (pf:imp-intro (pf:imp-intro
         (pf:imp-elim
           (pf:all-elim (pf:hyp 1) (Power_closed (UnivOf (pf:var 0))))
           (pf:imp-intro (pf:imp-intro (pf:hyp 0)))))))))

; Its proof introduces and eliminates quantifiers over all sets, and `set`,
; the sort of all sets, is above every universe: those rules are compiled to
; the rule constants `allI@set` and `allE@set`, which the family does not
; decode.  The quantifier over propositions inside `and` is still decoded.
!(let $result (try (hol:native-proof universe-power-closure)) (car-atom $result))

; The resulting proof is usable by an ordinary dependent consumer. Its
; result type retains the particular proof, not merely a Boolean success.
!(let $package (hol:native-proof universe-power-closure)
   (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (HolNativeUseV1 $checked $consumer $result $result-type)
          (hol:native-use $package (Lam $type (Refl (idx 0))))
       (let (Id $carrier $left $right) $result-type
         (UniverseDependentUse
           (== $checked $package) (== $left $term) (== $right $term)
           (size-atom $assumptions))))))

; Wrong proof, wrong carrier, and withdrawn source authority are distinct
; failures. None may manufacture an established dependent result.
!(let $result (try (hol:proves (pf:known UnivOf_In)
   (all set (lam N (Power_closed (UnivOf N))))))
   (car-atom $result))
!(let $package (hol:native-proof universe-power-closure)
   (let $result (try (hol:native-use $package
     (Lam (Sort (LevelConst 0)) (Refl (idx 0)))))
     (car-atom $result)))
!(let $package (hol:native-proof universe-power-closure)
   (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let $removed
          (match &self
            (hol:known UnivOf_ZF_closed $p $origin $tree $depends $revision $signature)
            (remove-atom &self
              (hol:known UnivOf_ZF_closed $p $origin $tree $depends $revision $signature)))
       (let $result (try (hol:native-use $package (Lam $type (Refl (idx 0)))))
         (car-atom $result)))))
