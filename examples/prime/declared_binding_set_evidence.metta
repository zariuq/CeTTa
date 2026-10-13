; A declared language builds a function, the HOL guest proves its equation,
; and an explicit identity interpretation makes that proof usable by a
; dependent computation. The original language instance and source proof
; remain available beside the computed value.
(hol:profile base)
!(import! &self pf)
!(import! &self "../../langdef/prime/declared_binding_package_v1.metta")
!(import! &self "../../langdef/prime/declared_binding_dependent_package_v1.metta")

!(hol:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
; These are explicit source equality assumptions. The identity link below
; realizes the used reflexivity assumption as native identity evidence.
!(pf:equality &self num refl@num subst@num)

!(bind! &successor-source
   (binding:prepare @(binding:lambda $x (binding:result (suc $x)))))

; The function is produced by the authored GSLT lowering, then registered
; as the mathematical definition used by the remaining judgments.
!(let $instance (langdef:lowering-source &successor-source)
   (let (quote $f) (binding:dependent-code () $instance)
     (hol:define &self generatedSuccessor (-> num num)
       (= generatedSuccessor $f))))
!(let $f (binding:function &successor-source)
   (FromDeclaredLanguage ($f (suc zero))))
!(type:check &self generatedSuccessor (-> num num))
!(hol:check &self generatedSuccessor (-> num num))
!(lang:native-type prime (generatedSuccessor zero) num)

!(pf:theorem &self generated-successor-law
   (all num (lam x (eq num (generatedSuccessor x) (suc x))))
   (pf:fix x (pf:refl num (suc x))))

; The returned number is the index of the returned identity evidence.
; Changing the number without transporting that evidence is invalid.
!(hol:define &self retainAt
   (-> (x : num) (e : (id num (generatedSuccessor x) (suc x)))
       (sigma (y : num) (id num y (suc x))))
   (= (retainAt $x $e) (pair (generatedSuccessor $x) $e)))

; This consumer returns the proof itself, with identity evidence at that
; exact proof. A value or an acceptance Boolean cannot replace its input.
!(hol:define &self retainExact
   (-> (x : num) (e : (id num (generatedSuccessor x) (suc x)))
       (sigma (w : (id num (generatedSuccessor x) (suc x)))
         (id (id num (generatedSuccessor x) (suc x)) w e)))
   (= (retainExact $x $e) (pair $e (refl $e))))

; The native proof service consumes explicit typed terms. These two local
; adapters build its consumer and display its numeral result; they add no
; proof rules. The service rechecks the source package before using it.
(= (stack:number (DeclConst zero)) zero)
(= (stack:number (App (DeclConst suc) $n))
   (let $value (stack:number $n) (suc $value)))
(= (stack:one) (App (DeclConst suc) (DeclConst zero)))
(= (stack:consumer $proof-type $input $proof-index)
   (Lam $proof-type
     (App (App (DeclConst retainAt) $input)
          (App (idx 0) $proof-index))))
(= (stack:exact-consumer $proof-type $input $proof-index)
   (Lam $proof-type
     (App (App (DeclConst retainExact) $input)
          (App (idx 0) $proof-index))))
; This example's explicit typed representation retains the resolved index.
; Supplying the known num domain does not supply the missing ambient value.
(= (stack:open-number-code (quote (lam _ (suc (idx $position)))))
   (Lam (DeclConst num) (App (DeclConst suc) (idx $position))))

!(let $package (hol:native-proof generated-successor-law)
   (let $linked (hol:native-link identity $package)
     (let (HolNativeLinkedV1 $reading $checked $term $type $context $rules
           ((Realized $origin $key $realization))) $linked
       (let $one (stack:one)
         (let $consumer (stack:consumer $type $one $one)
           (let (HolNativeNormalFormV1 $used $consumer-code $application
                 (Pair $number (Refl $witness)) $before $after)
                (hol:native-normalize $linked $consumer)
             (let $value (stack:number $number)
               (DependentResult $value
                 (EvidenceAtSameValue (== $number $witness))
                 (SourceEquality $reading $origin Realized)))))))))

; Keep the complete source instance, assumptions, realizations and result.
; The following observation is only a display of that retained computation.
!(bind! &exact-evidence-run
   (let $package (hol:native-proof generated-successor-law)
   (let $linked (hol:native-link identity $package)
     (let (HolNativeLinkedV1 $reading $checked $term $type $context $rules $rows) $linked
       (let $one (stack:one)
         (let $consumer (stack:exact-consumer $type $one $one)
           (LangDef:DependentEvidenceRun
             (langdef:lowering-source &successor-source) $linked
             (hol:native-normalize $linked $consumer))))))))
!(let (LangDef:DependentEvidenceRun $source $linked
        (HolNativeNormalFormV1 $used $body $application
          (Pair (Refl $number) (Refl (Refl $same))) $before $after)) &exact-evidence-run
   (ExactEvidence (stack:number $number) (ProofIndexPreserved (== $number $same))))

!(let $instance (langdef:lowering-source &successor-source)
   (let (quote $receipts) (langdef:instance-bindings $instance)
     (RetainedBindingOccurrences (- (size-atom $receipts) 1))))

; An open component depends on base, not on its own parameter. Closing the
; actual instance abstracts that dependency and retains the original source.
!(bind! &contextual-source
   (let $signature (binding:signature)
     (let $open (langdef:instantiate-code $signature ()
           @(binding:lambda $x (binding:result (suc $base))){$base})
       (binding:abstract-instance $base $open))))
!(let (LangDef:ContextAbstraction $base $open $closed) &contextual-source
   (let (quote $open-code) (binding:dependent-code ($base) $open)
     (OpenDependencyPreserved (== $open-code (lam _ (suc (idx 1)))))))
!(let $closed (binding:abstracted-source &contextual-source)
   (let (quote $code) (binding:dependent-code () $closed)
     (hol:define &self generatedAt (-> num num num) (= generatedAt $code))))
!(generatedAt (suc zero) zero)
!(let $closed (binding:abstracted-source &contextual-source)
   (binding:dependent-check () $closed @(-> num num num)))
!(pf:theorem &self contextual-successor-law
   (all num (lam b (all num (lam x
     (eq num (generatedAt b x) (suc b))))))
   (pf:fix b (pf:fix x (pf:refl num (suc b)))))
!(hol:define &self retainExactAt
   (-> (b : num) (x : num) (e : (id num (generatedAt b x) (suc b)))
       (sigma (w : (id num (generatedAt b x) (suc b)))
         (id (id num (generatedAt b x) (suc b)) w e)))
   (= (retainExactAt $b $x $e) (pair $e (refl $e))))
(= (stack:context-consumer $proof-type $base $input $proof-base)
   (Lam $proof-type
     (App (App (App (DeclConst retainExactAt) $base) $input)
       (App (App (idx 0) $proof-base) $input))))
!(bind! &contextual-evidence-run
   (let $package (hol:native-proof contextual-successor-law)
   (let $linked (hol:native-link identity $package)
     (let (HolNativeLinkedV1 $reading $checked $term $type $context $rules $rows) $linked
       (let $one (stack:one)
         (let $consumer (stack:context-consumer $type $one (DeclConst zero) $one)
           (LangDef:DependentEvidenceRun &contextual-source $linked
             (hol:native-normalize $linked $consumer))))))))
!(let (LangDef:DependentEvidenceRun $source $linked
        (HolNativeNormalFormV1 $used $body $application
          (Pair (Refl $number) (Refl (Refl $same))) $before $after)) &contextual-evidence-run
   (ContextualEvidence (stack:number $number) (ProofIndexPreserved (== $number $same))))

; Private freshening and changing local spelling retain the checked term.
!(let $renamed (binding:prepare
       @(binding:lambda $renamed-local (binding:result (suc $renamed-local))))
   (let $left (langdef:lowering-source &successor-source)
     (let $right (langdef:lowering-source $renamed)
       (FreshRenaming (== (binding:dependent-code () $left)
                         (binding:dependent-code () $right))))))
; The same declared form can be built as data at runtime.
!(let $source (cons-atom quote
       ((binding:lambda $dynamic (binding:result (suc $dynamic)))))
   (let $built (binding:prepare $source)
     (let $left (langdef:lowering-source &successor-source)
       (let $right (langdef:lowering-source $built)
         (let $f (binding:function $built)
           (DynamicBinding (== (binding:dependent-code () $left)
                               (binding:dependent-code () $right))
             (binding:dependent-check () $right @(-> num num))
             ($f (suc zero))))))))

; The where value reads the ambient base before its own pattern exists.
!(let $source (binding:prepare
       @(binding:lambda $base (binding:lambda $x
          (binding:where $base $base (binding:result (refl $base))))))
   (let $instance (langdef:lowering-source $source)
     (ValueOutsideScope (binding:dependent-check () $instance
       @(-> (b : num) (x : num) (id num b b))))))
; Accidentally using x instead of the ambient base is well-formed code,
; but it fails the required dependent index.
!(let $source (binding:prepare
       @(binding:lambda $base (binding:lambda $x
          (binding:where $base $x (binding:result (refl $base))))))
   (let $instance (langdef:lowering-source $source)
     (AccidentalCapture (binding:dependent-check () $instance
       @(-> (b : num) (x : num) (id num b b))))))

; An open component cannot borrow a consumer's unrelated proof slot as its
; missing number dependency. The native service checks the retained context.
!(let (LangDef:ContextAbstraction $base $open $closed) &contextual-source
   (let $code (binding:dependent-code ($base) $open)
     (let $native (stack:open-number-code $code)
       (let $package (hol:native-proof generated-successor-law)
         (let $linked (hol:native-link identity $package)
           (let (HolNativeLinkedV1 $reading $checked $term $type $context $rules $rows) $linked
             (UnboundDependency
               (car-atom (try (hol:native-use $linked (Lam $type $native)))))))))))
; Erasing the required proof and putting a number in its place is refused.
!(let $package (hol:native-proof generated-successor-law)
   (let $linked (hol:native-link identity $package)
     (let (HolNativeLinkedV1 $reading $checked $term $type $context $rules $rows) $linked
       (let $one (stack:one)
         (let $consumer (Lam $type
               (App (App (DeclConst retainExact) $one) (DeclConst zero)))
           (RequiredEvidenceErasure (car-atom (try (hol:native-use $linked $consumer)))))))))

; A well-typed function cannot be assigned an unrelated result family.
!(type:check &self generatedSuccessor (-> num (id num zero zero)))
; The valid theorem does not prove that every successor is zero.
!(car-atom (try (hol:proves &self
   (pf:known generated-successor-law)
   (all num (lam x (eq num (generatedSuccessor x) zero))))))
; Evidence at zero cannot be supplied at one.
!(let $package (hol:native-proof generated-successor-law)
   (let $linked (hol:native-link identity $package)
     (let (HolNativeLinkedV1 $reading $checked $term $type $context $rules $rows) $linked
       (let $one (stack:one)
         (let $consumer (stack:consumer $type $one (DeclConst zero))
           (let $outcome (try (hol:native-use $linked $consumer))
             (WrongIndex (car-atom $outcome))))))))
; The source equation and native identity have a declared interpretation.
; Without it, the same dependent consumer is refused.
!(let $package (hol:native-proof generated-successor-law)
   (let (HolNativeProofV1 $name $proposition $source-proof $term $type
         $context $rules $assumptions $digest) $package
     (let $consumer (stack:consumer $type (DeclConst zero) (DeclConst zero))
       (let $outcome (try (hol:native-use $package $consumer))
         (Unlinked (car-atom $outcome))))))
