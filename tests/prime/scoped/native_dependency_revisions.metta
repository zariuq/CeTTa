(hol:profile core)

; A proof whose goal reaches a definition through two aliases. All three
; definitions must remain in its native context and ordered rule package.
!(hol:define dep-leaf prop (= dep-leaf (imp Falsum Falsum)))
!(hol:define dep-middle prop (= dep-middle dep-leaf))
!(hol:define dep-root prop (= dep-root dep-middle))
!(hol:theorem dep-proof dep-root (pf:imp-intro (pf:hyp 0)))

; The saved package survives unrelated definitions and still computes.
!(let $package (hol:native-proof dep-proof)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $added (hol:define dep-unused (-> prop prop) (= (dep-unused $p) $p))
   (let (HolNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
       (hol:native-normalize $package (Lam $type (Refl (idx 0))))
    (TransitiveDependencyUse (== $checked $package)
      (== $normal (Refl $term)) (size-atom $assumptions))))))

; Changing a transitively reached rule invalidates use of the saved package.
; The mutation is confined to this test's private theory space.
!(let $package (hol:native-proof dep-proof)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $changed (match &self (type:rule dep-leaf $arity $patterns $rhs)
        (let $removed (remove-atom &self (type:rule dep-leaf $arity $patterns $rhs))
          (add-atom &self (type:rule dep-leaf $arity $patterns (DeclConst Falsum)))))
   (let $result (try (hol:native-normalize $package (Lam $type (Refl (idx 0)))))
    (ChangedTransitiveDependency (car-atom $result))))))

; Both alternatives and their pattern-only constructor dependencies survive
; collection, even though each proof uses just one of the two constructors.
!(hol:inductive DepChoice (u 0) (: DepLeft DepChoice) (: DepRight DepChoice))
!(hol:define dep-choose (-> DepChoice prop)
   (= (dep-choose DepLeft) (imp Falsum Falsum))
   (= (dep-choose DepRight) (all prop (lam p (imp p p)))))
!(hol:theorem dep-left-proof (dep-choose DepLeft) (pf:imp-intro (pf:hyp 0)))
!(hol:theorem dep-right-proof (dep-choose DepRight)
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(map-atom (dep-left-proof dep-right-proof) $theorem
  (let $package (hol:native-proof $theorem)
   (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
    (let (HolNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
       (hol:native-normalize $package (Lam $type (Refl (idx 0))))
     (PatternDependencies $name (== $normal (Refl $term)))))))

; A later program can use its own declared functions. These are consumer
; dependencies, not retroactive dependencies of the imported proof package.
!(let $package (hol:native-proof dep-left-proof)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $declared (hol:define dep-program-id (-> (A : (u 0)) (x : A) A)
                   (= (dep-program-id $A $x) $x))
   (let (HolNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
      (hol:native-normalize $package
        (Lam $type (Refl (App (App (DeclConst dep-program-id) $type) (idx 0)))))
    (let (Id $carrier $left $right) $normal-type
     (LaterDeclaredConsumer (== $checked $package)
       (== $normal (Refl $term)) (== $left $term) (== $right $term)))))))

; Extending the consumer context is checked, not permission to invent constants.
!(let $package (hol:native-proof dep-left-proof)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $bad (try (hol:native-normalize $package
      (Lam $type (App (DeclConst dep-undeclared-program) (idx 0)))))
   (UndeclaredConsumer (car-atom $bad)))))

; A type dependency can change without changing the theorem or its displayed
; constructor declaration. Missing formation must invalidate the saved proof;
; restoring the declaration restores the identical package.
!(let $package (hol:native-proof dep-left-proof)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $removed (remove-atom &self (: DepChoice (u 0)))
   (let $damaged (add-atom &self (: DepChoice dep-unformed-carrier))
    (let $bad (try (hol:native-use $package (Lam $type (Refl (idx 0)))))
     (let $removed-bad (remove-atom &self (: DepChoice dep-unformed-carrier))
      (let $restored (add-atom &self (: DepChoice (u 0)))
       (HiddenFormationRevision (car-atom $bad)
         (== (hol:native-proof dep-left-proof) $package)))))))))

; A consumer's ambient declaration cannot rebind the package-private family:
; a declaration of a reserved identity is invisible, so the consumer is
; checked against the package's own family.
!(let $package (hol:native-proof dep-left-proof)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) $formula) $type
   (let $shadow (add-atom &self (: $family (-> prop (u 0))))
    (let $bad (try (hol:native-use $package (Lam $type (DeclConst $family))))
     (PrivateFamilyShadow (car-atom $bad)))))))
; The same holds when the ambient declaration gives the family a type under
; which the consumer would be ill-typed.
!(let $package (hol:native-proof dep-left-proof)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) $formula) $type
   (let $shadow (add-atom &self (: $family (u 0)))
    (let $used (try (hol:native-use $package (Lam $type (App (DeclConst $family) $formula))))
     (FamilyAppliedUnderWrongShadow (car-atom $used)))))))
