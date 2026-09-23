; The four UnivOf axioms and their supporting definitions from Megalodon's
; ramsey36/preamble.mgs. They are admitted object-theory assumptions, not
; facts proved by the native dependent kernel. This tests the universe
; fragment, not admission of the complete HOTG preamble.
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
!(set:axiom UnivOf_In (all set (lam N (In N (UnivOf N)))))
!(set:axiom UnivOf_TransSet (all set (lam N (TransSet (UnivOf N)))))
!(set:axiom UnivOf_ZF_closed (all set (lam N (ZF_closed (UnivOf N)))))
!(set:axiom UnivOf_Min
   (all set (lam N (all set (lam U
     (imp (In N U) (imp (TransSet U) (imp (ZF_closed U) (Subq (UnivOf N) U)))))))))

; All four source axioms have native proof-family representations. Each
; package retains exactly its admitted axiom as an assumption.
!(map-atom (UnivOf_In UnivOf_TransSet UnivOf_ZF_closed UnivOf_Min) $law
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        (set:native-proof $law)
     (UniverseAxiom $name (size-atom $assumptions))))

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
   (let $law (match &self (universe-operation $operation $name) $name)
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
; Constructing syntax is separate from invoking the checking judgment.
(= (universe-consumer
      (App (DeclConst $family) (App (App (DeclConst In) $value) $universe))
      $first $continuation)
   (Lam (App (DeclConst $family) (App (App (DeclConst In) $value) $universe))
     (App (Lam
       (Sigma (DeclConst set)
         (App (DeclConst $family) (App (App (DeclConst In) (idx 0)) $universe)))
       $continuation)
       (Pair $first (idx 0)))))

; Compute the pair, then independently check it at the dependent result type.
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
        (== $result-type (Sigma (DeclConst set)
          (App (DeclConst $family) (App (App (DeclConst In) (idx 0)) $universe))))
        (size-atom $assumptions)))))))

; A second consumer needs the certified pair, and computes its set component.
!(let $package (set:native-proof pipeline-membership)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst In) $value) $universe)) $type
   (let $consumer (universe-consumer $type $value (Fst (idx 0)))
    (let (SetNativeNormalFormV1 $checked $body $application $normal $before $after)
       (set:native-normalize $package $consumer)
      (UniverseValue (== $normal $value) (== $after (DeclConst set))))))))

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
