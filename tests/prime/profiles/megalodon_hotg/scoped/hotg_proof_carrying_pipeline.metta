(set:profile megalodon-hotg)

; The four UnivOf axioms of Megalodon's ramsey36/preamble.mgs and their
; supporting definitions. Here they are theorems: each is one of the four
; universe laws of the signature, up to the definitions and the grouping of
; the closure conditions. This tests the universe fragment, not admission of
; the complete HOTG preamble.
!(set:define and (-> prop prop prop)
   (= (and $A $B) (all prop (lam p (imp (imp $A (imp $B p)) p)))))
!(set:define Subq (-> set set prop)
   (= (Subq $A $B) (all set (lam x (imp (In x $A) (In x $B))))))
!(set:define TransSet (-> set prop)
   (= (TransSet $U) (all set (lam x (imp (In x $U) (Subq x $U))))))
!(set:define Union_closed (-> set prop)
   (= (Union_closed $U) (all set (lam X (imp (In X $U) (In (Union X) $U))))))
!(set:define Power_closed (-> set prop)
   (= (Power_closed $U) (all set (lam X (imp (In X $U) (In (Power X) $U))))))
!(set:define Repl_closed (-> set prop)
   (= (Repl_closed $U)
      (all set (lam X (imp (In X $U)
        (all (-> set set) (lam F (imp
          (all set (lam x (imp (In x X) (In (F x) $U))))
          (In (Repl X F) $U)))))))))
!(set:define ZF_closed (-> set prop)
   (= (ZF_closed $U)
      (and (and (Union_closed $U) (Power_closed $U)) (Repl_closed $U))))
!(set:theorem UnivOf_In (all set (lam N (In N (UnivOf N)))) (pf:known universeIn))
!(set:theorem UnivOf_TransSet (all set (lam N (TransSet (UnivOf N)))) (pf:known universeTransitive))
; ZF_closed groups the three closures as (Union and Power) and Repl, the law
; as Union and (Power and Repl): the proof takes one apart and builds the other.
!(set:theorem UnivOf_ZF_closed (all set (lam N (ZF_closed (UnivOf N))))
   (pf:all-intro (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeClosed) (pf:var 2)) (Union_closed (UnivOf (pf:var 2)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1))))) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeClosed) (pf:var 2)) (and (Power_closed (UnivOf (pf:var 2))) (Repl_closed (UnivOf (pf:var 2))))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))) (Power_closed (UnivOf (pf:var 2)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1)))))))) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeClosed) (pf:var 1)) (and (Power_closed (UnivOf (pf:var 1))) (Repl_closed (UnivOf (pf:var 1))))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))) (Repl_closed (UnivOf (pf:var 1)))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))))))))
!(set:theorem UnivOf_Min
   (all set (lam N (all set (lam U
     (imp (In N U) (imp (TransSet U) (imp (ZF_closed U) (Subq (UnivOf N) U))))))))
   (pf:all-intro (pf:all-intro (pf:imp-intro (pf:imp-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeMinimal) (pf:var 1)) (pf:var 0)) (pf:hyp 2)) (pf:hyp 1)) (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:hyp 1) (and (Union_closed (pf:var 1)) (Power_closed (pf:var 1)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1)))) (Union_closed (pf:var 1))) (pf:imp-intro (pf:imp-intro (pf:hyp 1))))) (pf:all-intro (pf:imp-intro (pf:imp-elim (pf:imp-elim (pf:hyp 0) (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim (pf:hyp 2) (and (Union_closed (pf:var 2)) (Power_closed (pf:var 2)))) (pf:imp-intro (pf:imp-intro (pf:hyp 1)))) (Power_closed (pf:var 2))) (pf:imp-intro (pf:imp-intro (pf:hyp 0))))) (pf:imp-elim (pf:all-elim (pf:hyp 2) (Repl_closed (pf:var 2))) (pf:imp-intro (pf:imp-intro (pf:hyp 0)))))))))))))))))

; All four have native proof-family representations. Each package retains
; exactly the universe law it is proved from as its one assumption.
!(map-atom (UnivOf_In UnivOf_TransSet UnivOf_ZF_closed UnivOf_Min) $law
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        (set:native-proof $law)
     (UniverseLaw $name (size-atom $assumptions))))

; Eliminate both conjunctions in ZF_closed to obtain Power_closed. The
; retained tree contains only the same implication/quantifier proof rules
; used by the native compiler, rather than a new trusted projection rule.
!(set:theorem universe-power-closure
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
!(let $result (try (set:native-proof universe-power-closure)) (car-atom $result))

; The resulting proof is usable by an ordinary dependent consumer. Its
; result type retains the particular proof, not merely a Boolean success.
!(let $package (set:native-proof universe-power-closure)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (SetNativeUseV1 $checked $consumer $result $result-type)
          (set:native-use $package (Lam $type (Refl (idx 0))))
       (let (Id $carrier $left $right) $result-type
         (UniverseDependentUse
           (== $checked $package) (== $left $term) (== $right $term)
           (size-atom $assumptions))))))


; Derive union closure from the same retained ZF-closure assumption.
!(set:theorem universe-union-closure
   (all set (lam N (Union_closed (UnivOf N))))
   (pf:all-intro
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known UnivOf_ZF_closed) (pf:var 0))
         (Union_closed (UnivOf (pf:var 0))))
       (pf:imp-intro (pf:imp-intro
         (pf:imp-elim
           (pf:all-elim (pf:hyp 1) (Union_closed (UnivOf (pf:var 0))))
           (pf:imp-intro (pf:imp-intro (pf:hyp 1)))))))))

; The program selects closure laws from its space. Each operation must earn
; membership of its computed expression before the next operation can run.
!(add-atom &self (universe-operation Power universe-power-closure))
!(add-atom &self (universe-operation Union universe-union-closure))
(= (universe-pipeline Nil $value $proof) (ProvedSet $value $proof))
(= (universe-pipeline (Cons $operation $rest) $value $proof)
   (let $law (match &self (universe-operation $operation $name) $name){$operation}
     (let $next-proof
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:known $law) Empty) $value) $proof)
       (case (set:proves $next-proof (In ($operation $value) (UnivOf Empty)))
         ((True (universe-pipeline $rest ($operation $value) $next-proof)))))))
!(let (ProvedSet $value $proof)
   (universe-pipeline (Cons Power (Cons Union (Cons Power Nil))) Empty
     (pf:all-elim (pf:known UnivOf_In) Empty))
   (set:theorem pipeline-membership (In $value (UnivOf Empty)) $proof))

; A native continuation can require a Sigma indexed by its first component.
; Constructing syntax is separate from invoking the checking judgment.  In
; the kernel's spelling the sort of all sets is (Sort (LevelAbove 0)).
(= (universe-consumer
      (App (DeclConst $family) (App (App (DeclConst In) $value) $universe))
      $first $continuation)
   (Lam (App (DeclConst $family) (App (App (DeclConst In) $value) $universe))
     (App (Lam
       (Sigma (Sort (LevelAbove 0))
         (App (DeclConst $family) (App (App (DeclConst In) (idx 0)) $universe)))
       $continuation)
       (Pair $first (idx 0)))))

; Compute the pair, then independently check it at the dependent result type.
; The package declares the three theorems the pipeline's proof cites by name,
; universe-power-closure, universe-union-closure and UnivOf_In; computing the
; pair goes through their proofs.
!(let $package (set:native-proof pipeline-membership)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst In) $value) $universe)) $type
   (let $consumer (universe-consumer $type $value (idx 0))
    (let (SetNativeNormalFormV1 $checked $body $application
       (Pair $first $second) $original-type $result-type)
       (set:native-normalize $package $consumer)
      (UniversePair
        (== $prop (In (Power (Union (Power Empty))) (UnivOf Empty)))
        (== $first $value)
        (== $result-type (Sigma (Sort (LevelAbove 0))
          (App (DeclConst $family) (App (App (DeclConst In) (idx 0)) $universe))))
        (size-atom $assumptions)))))))

; A second consumer needs the certified pair, and computes its set component.
!(let $package (set:native-proof pipeline-membership)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst In) $value) $universe)) $type
   (let $consumer (universe-consumer $type $value (Fst (idx 0)))
    (let (SetNativeNormalFormV1 $checked $body $application $normal $before $after)
       (set:native-normalize $package $consumer)
      (UniverseValue (== $normal $value) (== $after (Sort (LevelAbove 0)))))))))

; Changing the first component without a corresponding proof must fail.
!(let $package (set:native-proof pipeline-membership)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst In) $value) $universe)) $type
   (let $consumer (universe-consumer $type (App (DeclConst Power) $value) (idx 0))
    (let $verdict (try (set:native-normalize $package $consumer))
      (ChangedIndex (car-atom $verdict)))))))

; Missing and deliberately mis-associated laws do not authorize a step.
!(collapse (universe-pipeline (Cons Repl Nil) Empty
   (pf:all-elim (pf:known UnivOf_In) Empty)))
!(add-atom &self (universe-operation Empty universe-power-closure))
!(collapse (universe-pipeline (Cons Empty Nil) Empty
   (pf:all-elim (pf:known UnivOf_In) Empty)))

; Withdrawal invalidates both later proof lookup and a previously saved
; package, without claiming the mathematical proposition has become false.
!(let $package (set:native-proof pipeline-membership)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst In) $value) $universe)) $type
   (let $consumer (universe-consumer $type $value (Fst (idx 0)))
    (let $removed (match &self
      (set:known UnivOf_ZF_closed $p $origin $tree $depends $revision $signature)
      (remove-atom &self (set:known UnivOf_ZF_closed $p $origin $tree $depends $revision $signature)))
     (let $verdict (try (set:native-normalize $package $consumer))
      (Withdrawn (car-atom $verdict))))))))
!(let $verdict (try (set:native-proof pipeline-membership))
  (WithdrawnLookup (car-atom $verdict)))
