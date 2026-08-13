(gslt-presentation-v1 petta-typecheck-v2-guard
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber petta)
    (outcome-algebra (PTEstablished PTRefuted PTUndetermined PTIncomplete)
      (exclusive)
      (default PTUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default))
  (signature
    (operator TTCons 2)
    (operator TUnion 1)
    (operator TArrow 3)
    (operator TBrand 2)
    (operator TList 1)
    (operator VCons 2)
    (operator FS 1)
    (operator DCons 2)
    (operator Decl 2)
    (operator TNominal 1)
    (operator TNewtype 1)
    (operator TAlias 1)
    (operator TCtor 2)
    (operator VSym 1)
    (operator VApp 2)
    (operator Consistent 3)
    (operator ConsistentList 2)
    (operator UnionMember 2)
    (operator ValueHasType 2)
    (operator GuardPasses 2)
    (operator BaseSortOf 2)
    (operator SortConflicts 3)
    (operator ConflictsAll 3)
    (operator ConflictsAllL 3)
    (operator DefiniteMismatch 3)
    (operator MismatchAll 3)
    (operator DeclaredConflictSome 3)
    (operator DeclaredConflictRest 3)
    (operator HeadConflictSome 4)
    (operator HeadConflictRest 4)
    (operator ArgListLen 2)
    (operator ArgListLenDiffers 2)
    (operator BoundnessRefuted 2)
    (operator EnvDeclared 2)
    (operator EnvDeclaredList 2)
  )
  (equations)
  (rewrites
    (rule consistent-refl
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (Consistent ?t ?t EdgeExact)) (body))
    (rule consistent-dyn-left
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (Consistent TUndefined ?t EdgeDynamic)) (body))
    (rule consistent-dyn-right
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (Consistent ?t TUndefined EdgeDynamic)) (body))
    (rule consistent-union-right
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (Consistent ?a (TUnion ?ms) EdgeStructural)) (body (UnionMember ?m ?ms) (Consistent ?a ?m ?e)))
    (rule consistent-union-left
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (Consistent (TUnion ?ms) ?b EdgeStructural)) (body (UnionMember ?m ?ms) (Consistent ?m ?b ?e)))
    (rule consistent-brand
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (Consistent (TBrand ?n ?r) (TBrand ?n ?s) EdgeExact)) (body))
    (rule consistent-arrow
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (Consistent (TArrow ?m ?as ?r) (TArrow ?m ?bs ?s) EdgeStructural)) (body (ConsistentList ?as ?bs) (Consistent ?r ?s ?e)))
    (rule consistent-list-nil
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (ConsistentList TTNil TTNil)) (body))
    (rule consistent-list-cons
      (nik-rule-frame-v1
        (native-projection native-type-compatibility)
        (role calculus)) (head (ConsistentList (TTCons ?a ?as) (TTCons ?b ?bs))) (body (Consistent ?a ?b ?e) (ConsistentList ?as ?bs)))
    (rule union-member-here
      (nik-rule-frame-v1
        (native-projection native-union-membership)
        (role calculus)) (head (UnionMember ?t (TTCons ?t ?ts))) (body))
    (rule union-member-there
      (nik-rule-frame-v1
        (native-projection native-union-membership)
        (role calculus)) (head (UnionMember ?t (TTCons ?u ?ts))) (body (UnionMember ?t ?ts)))
    (rule has-type-num
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType VNum TNum)) (body))
    (rule has-type-str
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType VStr TStr)) (body))
    (rule has-type-true
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType VTrue TBool)) (body))
    (rule has-type-false
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType VFalse TBool)) (body))
    (rule has-type-wildcard
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType ?v TUndefined)) (body))
    (rule has-type-union
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType ?v (TUnion ?ms))) (body (UnionMember ?m ?ms) (ValueHasType ?v ?m)))
    (rule has-type-brand
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType ?v (TBrand ?n ?r))) (body (ValueHasType ?v ?r)))
    (rule has-type-nil-list
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType VNil (TList ?t))) (body))
    (rule has-type-cons-list
      (nik-rule-frame-v1
        (native-projection native-value-typing)
        (role calculus)) (head (ValueHasType (VCons ?v ?vs) (TList ?t))) (body (ValueHasType ?v ?t) (ValueHasType ?vs (TList ?t))))
    (rule guard-passes
      (nik-rule-frame-v1
        (native-projection native-guard-checking)
        (role calculus)) (head (GuardPasses ?v ?t)) (body (ValueHasType ?v ?t)))
    (rule base-sort-num
      (nik-rule-frame-v1
        (native-projection native-base-sort-classification)
        (role calculus)) (head (BaseSortOf VNum TNum)) (body))
    (rule base-sort-str
      (nik-rule-frame-v1
        (native-projection native-base-sort-classification)
        (role calculus)) (head (BaseSortOf VStr TStr)) (body))
    (rule base-sort-true
      (nik-rule-frame-v1
        (native-projection native-base-sort-classification)
        (role calculus)) (head (BaseSortOf VTrue TBool)) (body))
    (rule base-sort-false
      (nik-rule-frame-v1
        (native-projection native-base-sort-classification)
        (role calculus)) (head (BaseSortOf VFalse TBool)) (body))
    (rule sc-num-str
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts ?f TNum TStr)) (body))
    (rule sc-num-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts ?f TNum TBool)) (body))
    (rule sc-str-num
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts ?f TStr TNum)) (body))
    (rule sc-str-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts ?f TStr TBool)) (body))
    (rule sc-bool-num
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts ?f TBool TNum)) (body))
    (rule sc-bool-str
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts ?f TBool TStr)) (body))
    (rule sc-union-right
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts (FS ?f) ?s (TUnion ?ms))) (body (ConflictsAll ?f ?s ?ms)))
    (rule sc-union-left
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts (FS ?f) (TUnion ?ms) ?t)) (body (ConflictsAllL ?f ?ms ?t)))
    (rule sc-newtype-formal
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts (FS ?f) ?s (TNominal ?n))) (body (EnvDeclared ?n (TNewtype ?r)) (SortConflicts ?f ?s ?r)))
    (rule sc-alias-formal
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts (FS ?f) ?s (TNominal ?n))) (body (EnvDeclared ?n (TAlias ?r)) (SortConflicts ?f ?s ?r)))
    (rule sc-newtype-sort
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts (FS ?f) (TNominal ?n) ?t)) (body (EnvDeclared ?n (TNewtype ?r)) (SortConflicts ?f ?r ?t)))
    (rule sc-alias-sort
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (SortConflicts (FS ?f) (TNominal ?n) ?t)) (body (EnvDeclared ?n (TAlias ?r)) (SortConflicts ?f ?r ?t)))
    (rule ca-nil
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (ConflictsAll ?f ?s TTNil)) (body))
    (rule ca-cons
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (ConflictsAll ?f ?s (TTCons ?m ?ms))) (body (SortConflicts ?f ?s ?m) (ConflictsAll ?f ?s ?ms)))
    (rule cal-nil
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (ConflictsAllL ?f TTNil ?t)) (body))
    (rule cal-cons
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus)) (head (ConflictsAllL ?f (TTCons ?m ?ms) ?t)) (body (SortConflicts ?f ?m ?t) (ConflictsAllL ?f ?ms ?t)))
    (rule dm-base
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) ?v ?t)) (body (BaseSortOf ?v ?s) (SortConflicts ?f ?s ?t)))
    (rule dm-ctor
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) ?v (TCtor ?n ?k))) (body (BaseSortOf ?v ?s)))
    (rule dm-union
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) ?v (TUnion ?ms))) (body (MismatchAll ?f ?v ?ms)))
    (rule dm-list-literal
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) ?v (TList ?t))) (body (BaseSortOf ?v ?s)))
    (rule dm-list-here
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) (VCons ?v ?vs) (TList ?t))) (body (DefiniteMismatch ?f ?v ?t)))
    (rule dm-list-there
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) (VCons ?v ?vs) (TList ?t))) (body (DefiniteMismatch ?f ?vs (TList ?t))))
    (rule dm-newtype-formal
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) ?v (TNominal ?n))) (body (EnvDeclared ?n (TNewtype ?r)) (DefiniteMismatch ?f ?v ?r)))
    (rule dm-alias-formal
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) ?v (TNominal ?n))) (body (EnvDeclared ?n (TAlias ?r)) (DefiniteMismatch ?f ?v ?r)))
    (rule dm-declared-symbol
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) (VSym ?n) ?t)) (body (EnvDeclaredList ?n ?ds) (DeclaredConflictSome ?f ?ds ?t)))
    (rule dm-applied-head
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DefiniteMismatch (FS ?f) (VApp ?h ?k) ?t)) (body (EnvDeclaredList ?h ?ds) (HeadConflictSome ?f ?ds ?k ?t)))
    (rule ma-nil
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (MismatchAll ?f ?v TTNil)) (body))
    (rule ma-cons
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (MismatchAll ?f ?v (TTCons ?m ?ms))) (body (DefiniteMismatch ?f ?v ?m) (MismatchAll ?f ?v ?ms)))
    (rule dcs-cons
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DeclaredConflictSome ?f (DCons (Decl ?n ?d) ?ds) ?t)) (body (SortConflicts ?f ?d ?t) (DeclaredConflictRest ?f ?ds ?t)))
    (rule dcr-nil
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DeclaredConflictRest ?f DNil ?t)) (body))
    (rule dcr-cons
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (DeclaredConflictRest ?f (DCons (Decl ?n ?d) ?ds) ?t)) (body (SortConflicts ?f ?d ?t) (DeclaredConflictRest ?f ?ds ?t)))
    (rule hcs-here
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (HeadConflictSome ?f (DCons (Decl ?n (TArrow ?m ?as ?r)) ?ds) ?k ?t)) (body (ArgListLen ?as ?k) (SortConflicts ?f ?r ?t) (HeadConflictRest ?f ?ds ?k ?t)))
    (rule hcs-skip
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (HeadConflictSome ?f (DCons (Decl ?n (TArrow ?m ?as ?r)) ?ds) ?k ?t)) (body (ArgListLenDiffers ?as ?k) (HeadConflictSome ?f ?ds ?k ?t)))
    (rule hcr-nil
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (HeadConflictRest ?f DNil ?k ?t)) (body))
    (rule hcr-cons
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (HeadConflictRest ?f (DCons (Decl ?n (TArrow ?m ?as ?r)) ?ds) ?k ?t)) (body (ArgListLen ?as ?k) (SortConflicts ?f ?r ?t) (HeadConflictRest ?f ?ds ?k ?t)))
    (rule hcr-skip
      (nik-rule-frame-v1
        (native-projection native-definite-mismatch)
        (role calculus)) (head (HeadConflictRest ?f (DCons (Decl ?n (TArrow ?m ?as ?r)) ?ds) ?k ?t)) (body (ArgListLenDiffers ?as ?k) (HeadConflictRest ?f ?ds ?k ?t)))
    (rule all-nil
      (nik-rule-frame-v1
        (native-projection native-arity-classification)
        (role calculus)) (head (ArgListLen TTNil FZ)) (body))
    (rule all-cons
      (nik-rule-frame-v1
        (native-projection native-arity-classification)
        (role calculus)) (head (ArgListLen (TTCons ?a ?as) (FS ?k))) (body (ArgListLen ?as ?k)))
    (rule ald-nil-succ
      (nik-rule-frame-v1
        (native-projection native-arity-classification)
        (role calculus)) (head (ArgListLenDiffers TTNil (FS ?k))) (body))
    (rule ald-cons-zero
      (nik-rule-frame-v1
        (native-projection native-arity-classification)
        (role calculus)) (head (ArgListLenDiffers (TTCons ?a ?as) FZ)) (body))
    (rule ald-cons-rec
      (nik-rule-frame-v1
        (native-projection native-arity-classification)
        (role calculus)) (head (ArgListLenDiffers (TTCons ?a ?as) (FS ?k))) (body (ArgListLenDiffers ?as ?k)))
    (rule br-det-num
      (nik-rule-frame-v1
        (native-projection native-boundness-refutation)
        (role calculus)) (head (BoundnessRefuted MDet TNum)) (body))
    (rule br-det-str
      (nik-rule-frame-v1
        (native-projection native-boundness-refutation)
        (role calculus)) (head (BoundnessRefuted MDet TStr)) (body))
    (rule br-det-bool
      (nik-rule-frame-v1
        (native-projection native-boundness-refutation)
        (role calculus)) (head (BoundnessRefuted MDet TBool)) (body))
    (rule br-semidet-num
      (nik-rule-frame-v1
        (native-projection native-boundness-refutation)
        (role calculus)) (head (BoundnessRefuted MSemidet TNum)) (body))
    (rule br-semidet-str
      (nik-rule-frame-v1
        (native-projection native-boundness-refutation)
        (role calculus)) (head (BoundnessRefuted MSemidet TStr)) (body))
    (rule br-semidet-bool
      (nik-rule-frame-v1
        (native-projection native-boundness-refutation)
        (role calculus)) (head (BoundnessRefuted MSemidet TBool)) (body))
  ))
