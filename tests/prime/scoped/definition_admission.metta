; Definitions earn formation and native clause checking before publication.
!(bind! &thy (new-space))
!(let $r (try (set:define &thy missingId (-> MissingType MissingType)
                (= (missingId $x) $x))) (car-atom $r))
!(set:define &thy missingId (-> MissingType MissingType)
   (= (missingId $x) $x))
!(== (collapse (match &thy (: missingId $type) $type)) ())
!(== (collapse (match &thy (type:rule missingId $arity $pats $rhs) $rhs)) ())
!(== (collapse (match &thy (set:known missingId $p $o $f $d $r $s) $p)) ())

; The same declaration becomes admissible after its missing type is supplied.
!(add-atom &thy (: MissingType (u 0)))
!(add-atom &thy (: element MissingType))
!(add-atom &thy (: otherElement MissingType))
!(set:define &thy missingId (-> MissingType MissingType)
   (= (missingId $x) $x))
!(type:check &thy (missingId element) MissingType)
!(type:eq &thy (missingId element) element)

; Higher-order clauses retain the variables captured beneath authored binders.
!(set:define &thy keep (-> MissingType (-> MissingType MissingType))
   (= (keep $x $y) $x))
!(type:eq &thy (keep element (missingId element)) element)
!(set:define &thy applyFn (-> (-> MissingType MissingType) MissingType MissingType)
   (= (applyFn $f $x) ($f $x)))
!(type:eq &thy (applyFn (lam x x) element) element)
!(set:define &thy capture (-> MissingType (-> MissingType MissingType))
   (= (capture $x $y) (applyFn (lam z $x) $y)))
!(type:eq &thy (capture element (missingId element)) element)

; An unresolved formation check does not license a declaration.
!(let $r (try (set:define &thy valueDomain (-> element MissingType)
                (= (valueDomain $x) element))) (car-atom $r))
!(== (collapse (match &thy (: valueDomain $type) $type)) ())
; A checked invalid universe level is refuted, without publication.
!(let $r (try (set:define &thy badLevel (-> (u -1) MissingType)
                (= (badLevel $x) element))) (car-atom $r))
!(== (collapse (match &thy (: badLevel $type) $type)) ())
!(add-atom &thy (: Large (u 1)))
!(add-atom &thy (: largeElement Large))
!(let $r (try (set:define &thy wrongResult (-> MissingType MissingType)
                (= (wrongResult $x) largeElement))) (car-atom $r))
!(== (collapse (match &thy (: wrongResult $type) $type)) ())
!(set:define &thy largeId (-> Large Large) (= (largeId $x) $x))
!(type:eq &thy (largeId largeElement) largeElement)

; Universe-valued definitions consume the type retained by native formation.
!(type:formed &thy (-> (u 0) (u 0)))
!(set:define &thy typeId (-> (u 0) (u 0)) (= (typeId $x) $x))
!(== (collapse (match &thy (: typeId $type) $type)) ((-> (u 0) (u 0))))
!(type:eq &thy (typeId MissingType) MissingType)
!(type:check &thy (typeId MissingType) (u 0))
; The universe U0 is not an element of itself.
!(type:check &thy (typeId (u 0)) (u 0))
!(type:eq &thy (typeId (u 0)) (u 0))
!(set:define &thy typeId1 (-> (u 1) (u 1)) (= (typeId1 $x) $x))
!(type:eq &thy (typeId1 Large) Large)
!(type:check &thy (typeId1 MissingType) (u 1))
; Dependent clauses retain their telescope through admission and execution.
!(type:formed &thy (-> (A : (u 0)) (x : A) A))
!(type:check &thy (lam A (lam x x)) (-> (A : (u 0)) (x : A) A))
!(type:check &thy (lam A (lam x A)) (-> (A : (u 0)) (x : A) A))
!(set:define &thy dependentId (-> (A : (u 0)) (x : A) A)
   (= (dependentId $A $x) $x))
!(== (collapse (match &thy (: dependentId $type) $type))
     ((-> (A : (u 0)) (x : A) A)))
!(type:check &thy (dependentId MissingType element) MissingType)
!(type:eq &thy (dependentId MissingType element) element)
!(set:define &thy dependentApply
   (-> (A : (u 0)) (B : (u 0)) (f : (-> (x : A) B)) (x : A) B)
   (= (dependentApply $A $B $f $x) ($f $x)))
!(type:check &thy (dependentApply MissingType MissingType (lam x x) element) MissingType)
!(type:eq &thy (dependentApply MissingType MissingType (lam x x) element) element)
!(set:define &thy dependentCapture (-> (A : (u 0)) (x : A) (y : A) A)
   (= (dependentCapture $A $x $y)
      (dependentApply $A $A (lam z $x) $y)))
!(type:eq &thy (dependentCapture MissingType element (missingId element)) element)
!(type:eq &thy (dependentCapture MissingType element otherElement) element)
!(type:eq &thy (dependentCapture MissingType element otherElement) otherElement)

; Native identity evidence can be constructed by an ordinary definition.
!(set:define &thy reflexiveEvidence
   (-> (A : (u 0)) (x : A) (id A x x))
   (= (reflexiveEvidence $A $x) (refl $x)))
!(type:check &thy (reflexiveEvidence MissingType element) (id MissingType element element))
!(type:eq &thy (reflexiveEvidence MissingType element) (refl element))

; The displayed result type itself computes by applying the supplied family.
!(set:define &thy retainIndexed
   (-> (A : (u 0)) (x : A) (P : (-> (z : A) (u 0))) (w : (P x)) (P x))
   (= (retainIndexed $A $x $P $w) $w))
!(type:check &thy (retainIndexed MissingType element (lam z MissingType) element) MissingType)
!(type:eq &thy (retainIndexed MissingType element (lam z MissingType) element) element)

; Neither a type parameter nor an unresolved clause variable is a value of A.
!(let $r (try (set:define &thy wrongDependent (-> (A : (u 0)) (x : A) A)
                (= (wrongDependent $A $x) $A))) (car-atom $r))
!(== (collapse (match &thy (: wrongDependent $type) $type)) ())
!(let $r (try (set:define &thy danglingDependent (-> (A : (u 0)) (x : A) A)
                (= (danglingDependent $A $x) $missing))) (car-atom $r))
!(== (collapse (match &thy (: danglingDependent $type) $type)) ())
!(type:check &thy (dependentId MissingType largeElement) MissingType)
!(type:check &thy (dependentId (u 0) MissingType) (u 0))
!(type:check &thy (retainIndexed MissingType element (lam z Large) element) Large)

; Transparent type aliases compose before a dependent function or pair uses
; them. The second projection keeps the computed first projection as its type.
!(set:define &thy AliasFirst (u 0) (= AliasFirst MissingType))
!(set:define &thy AliasSecond (u 0) (= AliasSecond AliasFirst))
!(type:check &thy (dependentId AliasSecond element) AliasSecond)
!(type:eq &thy (dependentId AliasSecond element) element)
!(set:define &thy aliasedPack (sigma (T : (u 0)) T)
   (= aliasedPack (pair AliasSecond element)))
!(type:check &thy aliasedPack (sigma (T : (u 0)) T))
!(type:eq &thy (fst aliasedPack) MissingType)
!(type:check &thy (snd aliasedPack) (fst aliasedPack))
!(type:check &thy (snd aliasedPack) MissingType)
!(type:eq &thy (snd aliasedPack) element)
!(type:check &thy (snd aliasedPack) Large)
; Conversion of a body is insufficient: publication must also check its type.
!(let $r (try (set:define &thy badAliasedPack (sigma (T : (u 0)) T)
                (= badAliasedPack (pair AliasSecond largeElement)))) (car-atom $r))
!(== (collapse (match &thy (: badAliasedPack $type) $type)) ())
!(== (collapse (match &thy (type:rule badAliasedPack $arity $pats $rhs) $rhs)) ())

; A longer ordered library is consumed beneath dependent and higher-order
; binders. Projection computes the family index before the caller uses it.
!(set:define &thy AliasThird (u 0) (= AliasThird AliasSecond))
!(set:define &thy makeAliasPack
   (-> (x : AliasThird) (sigma (T : (u 0)) T))
   (= (makeAliasPack $x) (pair AliasThird $x)))
!(type:check &thy (makeAliasPack element) (sigma (T : (u 0)) T))
!(type:check &thy (snd (makeAliasPack element)) (fst (makeAliasPack element)))
!(type:check &thy (snd (makeAliasPack element)) AliasThird)
!(type:eq &thy (fst (makeAliasPack element)) MissingType)
!(type:eq &thy (snd (makeAliasPack element)) element)
!(set:define &thy useAliasPack
   (-> (f : (-> AliasThird AliasThird)) (x : AliasThird) AliasThird)
   (= (useAliasPack $f $x) ($f (snd (makeAliasPack $x)))))
!(type:check &thy (useAliasPack (lam z z) element) MissingType)
!(type:eq &thy (useAliasPack (lam z z) element) element)
!(type:eq &thy (useAliasPack (lam z element) otherElement) element)
!(type:eq &thy (useAliasPack (lam z element) otherElement) otherElement)
!(type:check &thy (reflexiveEvidence AliasThird (snd (makeAliasPack element)))
   (id MissingType element element))
!(type:check &thy (reflexiveEvidence AliasThird (snd (makeAliasPack element)))
   (id MissingType element otherElement))
!(type:check &thy (makeAliasPack largeElement) (sigma (T : (u 0)) T))
!(type:check &thy (useAliasPack (lam z z) largeElement) AliasThird)

; A definition may name a dependent function without consuming its binders.
; Two transparent definitions lead to the native identity eliminator.
!(set:define &thy JFirst
   (-> (A : (u 0)) (x : A)
       (P : (-> (y : A) (q : (id A x y)) (u 0)))
       (d : (P x (refl x))) (y : A) (q : (id A x y)) (P y q))
   (= JFirst id:eliminate))
!(set:define &thy JSecond
   (-> (A : (u 0)) (x : A)
       (P : (-> (y : A) (q : (id A x y)) (u 0)))
       (d : (P x (refl x))) (y : A) (q : (id A x y)) (P y q))
   (= JSecond JFirst))
!(type:check &thy
   (JSecond MissingType element (lam y (lam q MissingType)) element element (refl element))
   MissingType)
!(type:eq &thy
   (JSecond MissingType element (lam y (lam q MissingType)) element element (refl element))
   element)
!(type:check &thy
   (JSecond MissingType element (lam y (lam q (id MissingType element y)))
      (refl element) element (refl element))
   (id MissingType element element))
!(type:eq &thy
   (JSecond MissingType element (lam y (lam q (id MissingType element y)))
      (refl element) element (refl element))
   (refl element))
!(type:check &thy
   (JSecond MissingType element (lam y (lam q MissingType)) element element (refl otherElement))
   MissingType)

; Authored equation arity may stop before the result's remaining Pi binders.
; Returned closures retain their captured argument, including under a caller.
!(set:define &thy keepClosure (-> MissingType MissingType MissingType)
   (= (keepClosure $x) (lam y $x)))
!(type:check &thy (keepClosure element) (-> MissingType MissingType))
!(type:eq &thy (keepClosure element otherElement) element)
!(type:eq &thy (keepClosure element otherElement) otherElement)
!(set:define &thy closureIdentity (-> MissingType MissingType)
   (= closureIdentity (lam z (keepClosure z otherElement))))
!(set:define &thy lambdaIdentity (-> MissingType MissingType)
   (= lambdaIdentity (lam z z)))
!(type:eq &thy closureIdentity lambdaIdentity)
; A saturated clause does not unfold its unapplied head to a lambda.
!(type:eq &thy closureIdentity missingId)
!(set:define &thy dependentClosure (-> (A : (u 0)) (x : A) (y : A) A)
   (= (dependentClosure $A $x) (lam y $x)))
!(type:check &thy (dependentClosure MissingType element) (-> MissingType MissingType))
!(type:eq &thy (dependentClosure MissingType element otherElement) element)

; Partial arity does not weaken body checking or permit unguarded recursion.
!(let $r (try (set:define &thy wrongClosure (-> MissingType MissingType MissingType)
                (= (wrongClosure $x) (lam y largeElement)))) (car-atom $r))
!(== (collapse (match &thy (: wrongClosure $type) $type)) ())
!(== (collapse (match &thy (type:rule wrongClosure $arity $pats $rhs) $rhs)) ())
!(let $r (try (set:define &thy oversaturated (-> MissingType MissingType)
                (= (oversaturated $x $y) $x))) (car-atom $r))
!(== (collapse (match &thy (: oversaturated $type) $type)) ())
!(let $r (try (set:define &thy selfValue MissingType
                (= selfValue selfValue))) (car-atom $r))
!(== (collapse (match &thy (: selfValue $type) $type)) ())
!(let $r (try (set:define &thy selfCall MissingType
                (= selfCall (selfCall)))) (car-atom $r))
!(== (collapse (match &thy (: selfCall $type) $type)) ())
!(== (collapse (match &thy (type:rule selfCall $arity $pats $rhs) $rhs)) ())

; A constructor-recursive definition can also return a function. Recursion
; still has to use a strictly smaller constructor field, even inside a lambda.
!(set:inductive &thy IterationNat (u 0)
   (: iterationZero IterationNat) (: iterationSucc (-> IterationNat IterationNat)))
!(set:define &thy iterateIdentity (-> IterationNat MissingType MissingType)
   (= (iterateIdentity iterationZero) (lam x x))
   (= (iterateIdentity (iterationSucc $n)) (lam x (iterateIdentity $n x))))
!(type:check &thy (iterateIdentity (iterationSucc iterationZero))
   (-> MissingType MissingType))
!(type:eq &thy (iterateIdentity (iterationSucc (iterationSucc iterationZero)) element) element)
!(type:eq &thy (iterateIdentity (iterationSucc iterationZero) otherElement) otherElement)
!(type:eq &thy (iterateIdentity (iterationSucc iterationZero) otherElement) element)
!(let $r (try (set:define &thy nondecreasingClosure (-> IterationNat MissingType MissingType)
                (= (nondecreasingClosure iterationZero) (lam x x))
                (= (nondecreasingClosure (iterationSucc $n))
                   (lam x (nondecreasingClosure (iterationSucc $n) x)))))
   (car-atom $r))
!(== (collapse (match &thy (: nondecreasingClosure $type) $type)) ())
!(== (collapse (match &thy (type:rule nondecreasingClosure $arity $pats $rhs) $rhs)) ())
