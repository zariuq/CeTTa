; Proofs over a carrier above the least universe, such as all sets, by rule
; constants.  The proof family decodes a quantifier only over a carrier of
; the least universe: over a larger one the decoding would identify a small
; type with a larger one.  There the native compiler introduces and
; eliminates the quantifier by the rule constants of the signature, at the
; carrier's instance:
;
;   allI@set : Π (P : set → prop). (Π (x : set). Holds (P x)) → Holds (all@set P)
;   allE@set : Π (P : set → prop). Holds (all@set P) → Π (x : set). Holds (P x)
;
; They have no equation: the kernel computes nothing with them.

; Whether a rule list has a rule for the given patterns.
(= (decodes LNil $patterns) False)
(= (decodes (LCons (PrimeRule $head $arity $rule-patterns $rhs) $rest) $patterns)
   (if (== $rule-patterns $patterns) True (decodes $rest $patterns)))

; Nothing is a member of the empty set, for every set, from the law emptyLaw.
!(set:theorem nothing-in-empty (all set (lam x (imp (In x Empty) Falsum)))
   (pf:all-intro (pf:imp-intro
     (pf:imp-elim (pf:all-elim (pf:known emptyLaw) (pf:var 0)) (pf:hyp 0)))))
; Its package introduces the quantifier by `allI@set`, eliminates the seed by
; `allE@set`, and keeps emptyLaw as its one assumption.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof nothing-in-empty)
   (let (App (App (DeclConst $intro) $predicate)
          (Lam $carrier (Lam $hypothesis
            (App (App (App (App (DeclConst $elim) $seed-predicate) $seed) $witness)
                 $premise))))
        $term
     (RuleConstants $name $intro $elim (size-atom $assumptions))))
; Its rules decode nothing over all sets; they decode the quantifier over
; propositions, which Falsum's definition uses, and implication.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof nothing-in-empty)
   (Decodes (decodes $rules ((App (DeclConst all@set) (PVar 0))))
            (decodes $rules ((App (DeclConst all@prop) (PVar 0))))
            (decodes $rules ((App (App (DeclConst imp) (PVar 0)) (PVar 1))))))

; The package is used at actual sets: at Power Empty, and at a universe, which
; is a set too.  Each use eliminates the quantifier by `allE@set`, and its
; type is the statement at that set, in normal form: a proof that the set is
; a member of Empty gives one of Falsum, every proposition.
!(let $package (set:native-proof nothing-in-empty)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let (SetNativeUseV1 $checked $consumer $result $result-type)
            (set:native-use $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (App (DeclConst Power) (DeclConst Empty)))))
         (let (Pi (App (DeclConst $holds) (App (App (DeclConst In) $member) (DeclConst Empty)))
                  (Pi (DeclConst prop) (App (DeclConst $falsity) (idx 0))))
              $result-type
           (UsedAt (== $checked $package) $member
             (== $holds $family) (== $falsity $family)))))))
!(let $package (set:native-proof nothing-in-empty)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let (SetNativeUseV1 $checked $consumer $result $result-type)
            (set:native-use $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (Sort (LevelConst 3)))))
         (let (Pi (App (DeclConst $holds) (App (App (DeclConst In) $member) (DeclConst Empty)))
                  (Pi (DeclConst prop) (App (DeclConst $falsity) (idx 0))))
              $result-type
           (UsedAt (== $checked $package) $member
             (== $holds $family) (== $falsity $family)))))))

; A step of the applied consumer keeps its type: the abstraction applied to
; the compiled proof steps to the elimination at Power Empty of the
; introduction, which is stuck, since the rule constants have no equation,
; and has the same type.
!(let $package (set:native-proof nothing-in-empty)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let (SetNativeNormalFormV1 $checked $body $application $normal $source-type
                                   $normal-type)
            (set:native-normalize $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (App (DeclConst Power) (DeclConst Empty)))))
         (let (App (App (App (DeclConst $elim) $p)
                        (App (App (DeclConst $intro) $q) $abstraction))
                   $at)
              $normal
           (BetaStep (== $checked $package) $elim $intro $at
             (== $normal-type $source-type)))))))

; Negative: the elimination at a predicate the proof is not about, at the
; sort of all sets, which is no set, and at a proposition, which is no set,
; are refuted; so is applying the proof as a function, which only the
; decoding of a small quantifier would make it.
!(let $package (set:native-proof nothing-in-empty)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let $result (try (set:native-use $package
            (Lam $type (App (App (App (DeclConst allE@set)
                                      (Lam (Sort (LevelAbove 0))
                                           (App (App (DeclConst In) (idx 0)) (idx 0))))
                                 (idx 0))
                            (DeclConst Empty)))))
       (WrongPredicate (car-atom $result)))))
!(let $package (set:native-proof nothing-in-empty)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let $result (try (set:native-use $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (Sort (LevelAbove 0))))))
         (NotASet (car-atom $result))))))
!(let $package (set:native-proof nothing-in-empty)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let $result (try (set:native-use $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (DeclConst Falsum)))))
         (NotASet (car-atom $result))))))
!(let $package (set:native-proof nothing-in-empty)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let $result (try (set:native-use $package
            (Lam $type (App (idx 0) (DeclConst Empty)))))
       (NotAFunction (car-atom $result)))))
; A hand-written package whose term eliminates at the wrong predicate is not
; the package of the proof: its use is refuted.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof nothing-in-empty)
   (let $forged
        (SetNativeProofV1 $name $prop $proof
          (App (App (DeclConst allI@set)
                    (Lam (Sort (LevelAbove 0)) (App (App (DeclConst In) (idx 0)) (idx 0))))
               (Lam (Sort (LevelAbove 0)) (idx 0)))
          $type $context $rules $assumptions $digest)
     (let $result (try (set:native-use $forged (Lam $type (Refl (idx 0)))))
       (Forged (car-atom $result)))))

; A run out of budget is incomplete, never refuted.
!(let $result (try (set:native-proof nothing-in-empty 3)) (car-atom $result))

; At a carrier of the least universe both presentations apply.  By unfolding,
; the quantifier over propositions is an abstraction; by rule constants
; throughout, asked for as `rule-constants`, it is `allI@prop` and the
; implication `impI`.  Both packages are established at the same proposition
; and type; the second decodes neither the quantifier nor implication, and
; is used as the first is.
!(set:theorem prop-self-implies (all prop (lam p (imp p p)))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof prop-self-implies)
   (let (Lam (DeclConst prop) (Lam $hypothesis (idx 0))) $term
     (Unfolding $name $prop)))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof prop-self-implies rule-constants)
   (let (App (App (DeclConst $intro) $predicate)
          (Lam (DeclConst prop)
            (App (App (App (DeclConst $implication) (idx 0)) (idx 0))
                 (Lam $hypothesis (idx 0)))))
        $term
     (let (SetNativeProofV1 $n $unfolded-prop $p $t $unfolded-type $c $r $a $d)
          (set:native-proof prop-self-implies)
       (RuleConstants $name $intro $implication $prop
         (== $prop $unfolded-prop) (== $type $unfolded-type)
         (decodes $rules ((App (DeclConst all@prop) (PVar 0))))
         (decodes $rules ((App (App (DeclConst imp) (PVar 0)) (PVar 1))))))))
!(let $package (set:native-proof prop-self-implies rule-constants)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (SetNativeUseV1 $checked $consumer $result (Id $carrier $left $right))
          (set:native-use $package (Lam $type (Refl (idx 0))))
       (RuleConstantsUse (== $checked $package) (== $left $term) (== $right $term)))))
