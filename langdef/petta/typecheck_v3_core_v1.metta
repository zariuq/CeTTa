; petta-typecheck-v3-core-v1 — QUALIFIED NATIVE CORE
;
; The four-axis success-typing core for typecheck-v3, in one-to-one rule
; correspondence with the sealed Lean specification
; (Mettapedia Languages/MeTTa/PeTTa/TypecheckV3Core.lean).  The Lean file
; carries the proofs; this presentation carries the same rules for the
; generation pipeline.  Keeping the names aligned is the drift alarm.
;
; The language manifest, checked generated plan, five-provider catalog, and
; isolated reference/compiled qualification harness are present.  The
; inherited providers carry the v2 wire vocabulary; explicit V3Elaborate*
; rules translate that shared contract into the native v3 calculus.
;
; Semantic notes mirrored from the Lean spec:
; - Rule ORDER for V3Consistent is semantic: unknown rules first, then the
;   actual-union (left/universal) rule, then required-union (right/
;   existential) rules — the Gentzen polarity the census recovered.
; - Joins: only the equal-case rules are pattern-expressible; the
;   conflict cases (distinctness-guarded widening/union) are mechanism
;   facts, like the census's inferred-*-widens-unknown mechanisms.
; - Grounding: there is deliberately NO rule producing V3GroundingLicensed
;   for V3StageSource or V3StageEvaluated — the absence is the discipline
;   (compiled-stage evidence only).

(gslt-presentation-v1 petta-typecheck-v3-core-v1
  (signature
    (operator V3CardLe 2)
    (operator V3Grade 1)
    (operator V3Effect 1)
    (operator V3ModeFits 2)
    (operator V3ConcretizeMode 2)
    (operator V3DirectEffectCard 4)
    (operator TTCons 2)
    (operator TUnion 1)
    (operator TProduct 1)
    (operator TArrow 3)
    (operator MEffect 1)
    (operator TBrand 2)
    (operator TList 1)
    (operator TNominal 1)
    (operator TNewtype 1)
    (operator TAlias 1)
    (operator V3ElaborateType 2)
    (operator V3ElaborateArgs 2)
    (operator V3ElaborateUnion 2)
    (operator V3ElaborateMode 2)
    (operator V3Alias 1)
    (operator EnvDeclared 2)
    (operator EnvDeclaredList 2)
    (operator EnvNonNewtype 1)
    (operator KnownExpressionEffect 2)
    (operator KnownExpressionResultType 2)
    (operator V3ResolveInput 2)
    (operator V3ResolveArgs 2)
    (operator V3ConsistentInput 2)
    (operator V3Prim 1)
    (operator V3List 1)
    (operator V3Product 1)
    (operator V3Arrow 3)
    (operator V3Union 2)
    (operator V3Newtype 2)
    (operator V3Consistent 2)
    (operator V3MayOverlap 2)
    (operator V3NonNominal 1)
    (operator V3ArgsCons 2)
    (operator V3ConsistentArgs 2)
    (operator V3JoinPublish 3)
    (operator V3JoinPrecise 3)
    (operator V3CardSeq 3)
    (operator V3FactMeet 3)
    (operator V3Facts 3)
    (operator V3FactsMeet 3)
    (operator V3FactsForType 2)
    (operator V3TypedResult 1)
    (operator V3RuntimeEvidence 4)
    (operator V3HeldEvidence 3)
    (operator V3SomeEvidence 1)
    (operator V3SourceQuotedProduct 1)
    (operator V3ExpressionEvidence 2)
    (operator V3AliasEvidence 2)
    (operator V3EnvCons 2)
    (operator V3IndexSucc 1)
    (operator V3ValueSource 1)
    (operator V3AliasSource 1)
    (operator V3BindingsCons 2)
    (operator V3EnvLookup 3)
    (operator V3ResolveBindingSource 3)
    (operator V3ApplyBindings 3)
    (operator V3LetEvidence 3)
    (operator V3BranchEvidence 3)
    (operator V3ConditionalEvidence 4)
    (operator V3SelectionEvidence 4)
    (operator V3FoldEmptyEvidence 2)
    (operator V3OnceCard 2)
    (operator V3AllEmptySuperposeEvidence 1)
    (operator V3EvidenceOutcome 4)
    (operator V3ExactTy 2)
    (operator V3ExactArgs 2)
    (operator V3SeamKind 4)
    (operator quote 1)
    (operator eval 1)
    (operator V3GroundingLicensed 2)
    (operator V3CallAccept 3)
  )
  (equations)
  (rewrites
    ; --- cardinality axis: eleven census mode-fits rules = one order ---
    ; (Lean: Card.le, modeFits, modeFits_iff_le)
    (rule v3-card-le-det-det
      (head (V3CardLe V3Det V3Det)) (body))
    (rule v3-card-le-det-semidet
      (head (V3CardLe V3Det V3Semidet)) (body))
    (rule v3-card-le-det-nondet
      (head (V3CardLe V3Det V3Nondet)) (body))
    (rule v3-card-le-semidet-semidet
      (head (V3CardLe V3Semidet V3Semidet)) (body))
    (rule v3-card-le-semidet-nondet
      (head (V3CardLe V3Semidet V3Nondet)) (body))
    (rule v3-card-le-nondet-nondet
      (head (V3CardLe V3Nondet V3Nondet)) (body))
    (rule v3-mode-fits-plain
      (head (V3ModeFits ?effect V3Plain)) (body))
    (rule v3-mode-fits-nondet-plain
      (head (V3ModeFits V3Plain (V3Grade V3Nondet))) (body))
    (rule v3-mode-fits-nondet-effect
      (head (V3ModeFits (V3Effect ?effect) (V3Grade V3Nondet))) (body))
    (rule v3-mode-fits-effect
      (head (V3ModeFits ?actual (V3Effect ?effect))) (body))
    (rule v3-mode-fits-demand
      (head (V3ModeFits (V3Grade ?effect) (V3Grade ?demand)))
      (body (V3CardLe ?effect ?demand)))
    ; Typed signatures retain named effects.  A runtime evidence artifact
    ; materializes only their conservative upper bound; call-site selection
    ; can still instantiate the retained signature more precisely.
    (rule v3-concretize-mode-grade
      (head (V3ConcretizeMode (V3Grade ?card) ?card)) (body))
    (rule v3-concretize-mode-plain
      (head (V3ConcretizeMode V3Plain V3Nondet)) (body))
    (rule v3-concretize-mode-effect
      (head (V3ConcretizeMode (V3Effect ?effect) V3Nondet)) (body))
    ; A direct higher-order parameter instantiates the enclosing named effect
    ; only when both arrows carry the same slot.  Unresolved and unrelated
    ; modes intentionally have no rule and therefore manufacture no grade.
    (rule v3-direct-effect-card
      (head (V3DirectEffectCard
              (V3Effect ?effect) (V3Effect ?effect)
              (V3Grade ?card) ?card))
      (body))

    ; --- inherited provider wire -> native v3 elaboration ---
    ; Provider relations keep their existing v2 wire meaning.  Nominal
    ; lookup is positive evidence; a missing/ambiguous/cyclic declaration
    ; yields no elaboration judgment and is handled as admission ignorance.
    (rule v3-elaborate-unknown
      (head (V3ElaborateType TUndefined V3Unknown)) (body))
    (rule v3-elaborate-num
      (head (V3ElaborateType TNum (V3Prim V3Num))) (body))
    (rule v3-elaborate-str
      (head (V3ElaborateType TStr (V3Prim V3Str))) (body))
    (rule v3-elaborate-bool
      (head (V3ElaborateType TBool (V3Prim V3Bool))) (body))
    ; PeTTa's declared Atom is a checker wildcard: it states no positive
    ; shape information.  Literal symbols acquire V3Prim V3Sym from runtime
    ; evidence instead; confusing the two would manufacture exactness.
    (rule v3-elaborate-atom-wildcard
      (head (V3ElaborateType TAtom V3Unknown)) (body))
    (rule v3-elaborate-list
      (head (V3ElaborateType (TList ?input) (V3List ?core)))
      (body (V3ElaborateType ?input ?core)))
    (rule v3-elaborate-product
      (head (V3ElaborateType
              (TProduct ?fields) (V3Product ?core-fields)))
      (body (V3ElaborateArgs ?fields ?core-fields)))
    (rule v3-elaborate-arrow
      (head (V3ElaborateType
              (TArrow ?mode ?inputs ?result)
              (V3Arrow ?core-mode-inputs ?core-mode ?core-result)))
      (body (V3ElaborateArgs ?inputs ?core-mode-inputs)
            (V3ElaborateMode ?mode ?core-mode)
            (V3ElaborateType ?result ?core-result)))
    (rule v3-elaborate-union
      (head (V3ElaborateType (TUnion ?members) ?core))
      (body (V3ElaborateUnion ?members ?core)))
    (rule v3-elaborate-union-singleton
      (head (V3ElaborateUnion (TTCons ?input TTNil) ?core))
      (body (V3ElaborateType ?input ?core)))
    (rule v3-elaborate-union-cons
      (head (V3ElaborateUnion
              (TTCons ?input (TTCons ?next ?rest))
              (V3Union ?core ?cores)))
      (body (V3ElaborateType ?input ?core)
            (V3ElaborateUnion (TTCons ?next ?rest) ?cores)))
    (rule v3-elaborate-nominal-abstract
      (head (V3ElaborateType
              (TNominal ?name) (V3Newtype ?name V3Unknown)))
      (body (EnvDeclared ?name TType)))
    (rule v3-elaborate-nominal-alias
      (head (V3ElaborateType (TNominal ?name) ?core))
      (body (EnvDeclared ?name (TAlias ?input))
            (V3ElaborateType ?input ?core)))
    (rule v3-elaborate-nominal-newtype
      (head (V3ElaborateType
              (TNominal ?name) (V3Newtype ?name ?core)))
      (body (EnvDeclared ?name (TNewtype ?input))
            (V3ElaborateType ?input ?core)))
    (rule v3-elaborate-brand
      (head (V3ElaborateType
              (TBrand ?name ?input) (V3Newtype ?name ?core)))
      (body (V3ElaborateType ?input ?core)))
    (rule v3-elaborate-args-nil
      (head (V3ElaborateArgs TTNil V3ArgsNil)) (body))
    (rule v3-elaborate-args-cons
      (head (V3ElaborateArgs
              (TTCons ?input ?inputs)
              (V3ArgsCons ?core ?cores)))
      (body (V3ElaborateType ?input ?core)
            (V3ElaborateArgs ?inputs ?cores)))
    (rule v3-elaborate-mode-plain
      (head (V3ElaborateMode MPlain V3Plain)) (body))
    (rule v3-elaborate-mode-det
      (head (V3ElaborateMode MDet (V3Grade V3Det))) (body))
    (rule v3-elaborate-mode-semidet
      (head (V3ElaborateMode MSemidet (V3Grade V3Semidet))) (body))
    (rule v3-elaborate-mode-nondet
      (head (V3ElaborateMode MNondet (V3Grade V3Nondet))) (body))
    (rule v3-elaborate-mode-effect
      (head (V3ElaborateMode (MEffect ?effect) (V3Effect ?effect)))
      (body))

    ; --- alias admission: total pre-pass into the alias-free core ---
    ; A missing EnvDeclared alias fact yields no resolution fact.  The caller maps
    ; that absence to an admission outcome, never to a consistency rejection.
    (rule v3-resolve-unknown
      (head (V3ResolveInput V3Unknown V3Unknown)) (body))
    (rule v3-resolve-prim
      (head (V3ResolveInput (V3Prim ?prim) (V3Prim ?prim))) (body))
    (rule v3-resolve-list
      (head (V3ResolveInput (V3List ?input) (V3List ?core)))
      (body (V3ResolveInput ?input ?core)))
    (rule v3-resolve-product
      (head (V3ResolveInput
              (V3Product ?fields) (V3Product ?core-fields)))
      (body (V3ResolveArgs ?fields ?core-fields)))
    (rule v3-resolve-arrow
      (head (V3ResolveInput
              (V3Arrow ?inputs ?effect ?result)
              (V3Arrow ?cores ?effect ?core-result)))
      (body (V3ResolveArgs ?inputs ?cores)
            (V3ResolveInput ?result ?core-result)))
    (rule v3-resolve-union
      (head (V3ResolveInput
              (V3Union ?left ?right)
              (V3Union ?core-left ?core-right)))
      (body (V3ResolveInput ?left ?core-left)
            (V3ResolveInput ?right ?core-right)))
    (rule v3-resolve-newtype
      (head (V3ResolveInput
              (V3Newtype ?brand ?repr)
              (V3Newtype ?brand ?core-repr)))
      (body (V3ResolveInput ?repr ?core-repr)))
    (rule v3-resolve-alias
      (head (V3ResolveInput (V3Alias ?name) ?core))
      (body (EnvDeclared ?name (TAlias ?input))
            (V3ElaborateType ?input ?core)))
    (rule v3-resolve-declared-abstract
      (head (V3ResolveInput
              (V3Alias ?name) (V3Newtype ?name V3Unknown)))
      (body (EnvDeclared ?name TType)))
    (rule v3-resolve-declared-newtype
      (head (V3ResolveInput
              (V3Alias ?name) (V3Newtype ?name ?core)))
      (body (EnvDeclared ?name (TNewtype ?input))
            (V3ElaborateType ?input ?core)))
    (rule v3-resolve-args-nil
      (head (V3ResolveArgs V3ArgsNil V3ArgsNil)) (body))
    (rule v3-resolve-args-cons
      (head (V3ResolveArgs
              (V3ArgsCons ?input ?inputs)
              (V3ArgsCons ?core ?cores)))
      (body (V3ResolveInput ?input ?core)
            (V3ResolveArgs ?inputs ?cores)))
    (rule v3-consistent-input
      (head (V3ConsistentInput ?actual ?required))
      (body (V3ResolveInput ?actual ?core-actual)
            (V3ResolveInput ?required ?core-required)
            (V3Consistent ?core-actual ?core-required)))

    ; --- shape axis: flow consistency (Lean: Consistent) ---
    (rule v3-consistent-unknown-left
      (head (V3Consistent V3Unknown ?type)) (body))
    (rule v3-consistent-unknown-right
      (head (V3Consistent ?type V3Unknown)) (body))
    (rule v3-consistent-refl
      (head (V3Consistent ?type ?type)) (body))
    (rule v3-consistent-union-actual
      (head (V3Consistent (V3Union ?left ?right) ?required))
      (body (V3Consistent ?left ?required)
            (V3Consistent ?right ?required)))
    (rule v3-consistent-union-required-left
      (head (V3Consistent ?actual (V3Union ?left ?right)))
      (body (V3Consistent ?actual ?left)))
    (rule v3-consistent-union-required-right
      (head (V3Consistent ?actual (V3Union ?left ?right)))
      (body (V3Consistent ?actual ?right)))
    (rule v3-consistent-list
      (head (V3Consistent (V3List ?elem) (V3List ?elem2)))
      (body (V3Consistent ?elem ?elem2)))
    (rule v3-consistent-product
      (head (V3Consistent
              (V3Product ?actuals) (V3Product ?requireds)))
      (body (V3ConsistentArgs ?actuals ?requireds)))
    (rule v3-consistent-arrow
      (head (V3Consistent (V3Arrow ?doms ?effect ?cod)
                          (V3Arrow ?doms2 ?effect2 ?cod2)))
      (body (V3ConsistentArgs ?doms ?doms2)
            (V3ModeFits ?effect ?effect2)
            (V3Consistent ?cod ?cod2)))
    (rule v3-consistent-args-nil
      (head (V3ConsistentArgs V3ArgsNil V3ArgsNil)) (body))
    (rule v3-consistent-args-cons
      (head (V3ConsistentArgs
              (V3ArgsCons ?actual ?actuals)
              (V3ArgsCons ?required ?requireds)))
      (body (V3Consistent ?actual ?required)
            (V3ConsistentArgs ?actuals ?requireds)))
    (rule v3-consistent-newtype
      (head (V3Consistent (V3Newtype ?brand ?repr)
                          (V3Newtype ?brand ?repr2)))
      (body (V3Consistent ?repr ?repr2)))

    ; --- explicit narrowing: symmetric possible runtime overlap ---
    ; Union choice is existential on both sides.  A single nominal boundary
    ; may expose its representation, but two nominal values must share a brand.
    (rule v3-may-overlap-union-actual-left
      (head (V3MayOverlap (V3Union ?left ?right) ?required))
      (body (V3MayOverlap ?left ?required)))
    (rule v3-may-overlap-union-actual-right
      (head (V3MayOverlap (V3Union ?left ?right) ?required))
      (body (V3MayOverlap ?right ?required)))
    (rule v3-may-overlap-union-required-left
      (head (V3MayOverlap ?actual (V3Union ?left ?right)))
      (body (V3MayOverlap ?actual ?left)))
    (rule v3-may-overlap-union-required-right
      (head (V3MayOverlap ?actual (V3Union ?left ?right)))
      (body (V3MayOverlap ?actual ?right)))
    (rule v3-may-overlap-newtype
      (head (V3MayOverlap (V3Newtype ?brand ?repr)
                          (V3Newtype ?brand ?repr2)))
      (body (V3MayOverlap ?repr ?repr2)))
    (rule v3-may-overlap-newtype-representation-left
      (head (V3MayOverlap (V3Newtype ?brand ?repr) ?other))
      (body (V3NonNominal ?other)
            (V3MayOverlap ?repr ?other)))
    (rule v3-may-overlap-newtype-representation-right
      (head (V3MayOverlap ?other (V3Newtype ?brand ?repr)))
      (body (V3NonNominal ?other)
            (V3MayOverlap ?other ?repr)))
    (rule v3-may-overlap-forward
      (head (V3MayOverlap ?actual ?required))
      (body (V3Consistent ?actual ?required)))
    (rule v3-may-overlap-reverse
      (head (V3MayOverlap ?actual ?required))
      (body (V3Consistent ?required ?actual)))
    (rule v3-non-nominal-prim
      (head (V3NonNominal (V3Prim ?prim))) (body))
    (rule v3-non-nominal-list
      (head (V3NonNominal (V3List ?element))) (body))
    (rule v3-non-nominal-product
      (head (V3NonNominal (V3Product ?fields))) (body))
    (rule v3-non-nominal-arrow
      (head (V3NonNominal (V3Arrow ?domains ?effect ?codomain))) (body))

    ; --- open-world axis: joins, equal cases (Lean: joinPublish/joinPrecise;
    ;     conflict cases are distinctness-guarded mechanisms) ---
    (rule v3-join-publish-equal
      (head (V3JoinPublish ?type ?type ?type)) (body))
    (rule v3-join-precise-equal
      (head (V3JoinPrecise ?type ?type ?type)) (body))

    ; --- H5 evidence transport: card sequencing, control flow, and staging ---
    (rule v3-card-seq-det-det
      (head (V3CardSeq V3Det V3Det V3Det)) (body))
    (rule v3-card-seq-det-semidet
      (head (V3CardSeq V3Det V3Semidet V3Semidet)) (body))
    (rule v3-card-seq-det-nondet
      (head (V3CardSeq V3Det V3Nondet V3Nondet)) (body))
    (rule v3-card-seq-semidet-det
      (head (V3CardSeq V3Semidet V3Det V3Semidet)) (body))
    (rule v3-card-seq-semidet-semidet
      (head (V3CardSeq V3Semidet V3Semidet V3Semidet)) (body))
    (rule v3-card-seq-semidet-nondet
      (head (V3CardSeq V3Semidet V3Nondet V3Nondet)) (body))
    (rule v3-card-seq-nondet-det
      (head (V3CardSeq V3Nondet V3Det V3Nondet)) (body))
    (rule v3-card-seq-nondet-semidet
      (head (V3CardSeq V3Nondet V3Semidet V3Nondet)) (body))
    (rule v3-card-seq-nondet-nondet
      (head (V3CardSeq V3Nondet V3Nondet V3Nondet)) (body))

    (rule v3-fact-meet-no-no
      (head (V3FactMeet V3No V3No V3No)) (body))
    (rule v3-fact-meet-no-yes
      (head (V3FactMeet V3No V3Yes V3No)) (body))
    (rule v3-fact-meet-yes-no
      (head (V3FactMeet V3Yes V3No V3No)) (body))
    (rule v3-fact-meet-yes-yes
      (head (V3FactMeet V3Yes V3Yes V3Yes)) (body))
    (rule v3-facts-meet
      (head (V3FactsMeet
              (V3Facts ?left-list ?left-bool ?left-expression)
              (V3Facts ?right-list ?right-bool ?right-expression)
              (V3Facts ?list ?bool ?expression)))
      (body (V3FactMeet ?left-list ?right-list ?list)
            (V3FactMeet ?left-bool ?right-bool ?bool)
            (V3FactMeet
              ?left-expression ?right-expression ?expression)))

    (rule v3-facts-unknown
      (head (V3FactsForType
              V3Unknown (V3Facts V3No V3No V3No))) (body))
    (rule v3-facts-num
      (head (V3FactsForType
              (V3Prim V3Num) (V3Facts V3No V3No V3No))) (body))
    (rule v3-facts-str
      (head (V3FactsForType
              (V3Prim V3Str) (V3Facts V3No V3No V3No))) (body))
    (rule v3-facts-bool
      (head (V3FactsForType
              (V3Prim V3Bool) (V3Facts V3No V3Yes V3No))) (body))
    (rule v3-facts-sym
      (head (V3FactsForType
              (V3Prim V3Sym) (V3Facts V3No V3No V3No))) (body))
    (rule v3-facts-list
      (head (V3FactsForType
              (V3List ?element) (V3Facts V3Yes V3No V3No))) (body))
    (rule v3-facts-product
      (head (V3FactsForType
              (V3Product ?fields) (V3Facts V3No V3No V3No))) (body))
    (rule v3-facts-arrow
      (head (V3FactsForType
              (V3Arrow ?domains ?mode ?result)
              (V3Facts V3No V3No V3Yes))) (body))
    (rule v3-facts-union
      (head (V3FactsForType
              (V3Union ?left ?right) (V3Facts V3No V3No V3No)))
      (body))
    (rule v3-facts-newtype
      (head (V3FactsForType
              (V3Newtype ?brand ?repr) (V3Facts V3No V3No V3No)))
      (body))

    (rule v3-expression-evidence-known
      (head (V3ExpressionEvidence ?expression
              (V3RuntimeEvidence V3StageEvaluated
                (V3TypedResult ?type) ?card ?facts)))
      (body (KnownExpressionEffect ?expression ?wire-mode)
            (V3ElaborateMode ?wire-mode (V3Grade ?card))
            (KnownExpressionResultType ?expression ?wire-type)
            (V3ElaborateType ?wire-type ?type)
            (V3FactsForType ?type ?facts)))
    (rule v3-expression-evidence-quoted-product
      (head (V3ExpressionEvidence
              (V3SourceQuotedProduct ?wire-fields)
              (V3RuntimeEvidence V3StageEvaluated
                (V3TypedResult (V3Product ?fields)) V3Det
                (V3Facts V3No V3No V3Yes))))
      (body (V3ElaborateArgs ?wire-fields ?fields)))
    (rule v3-expression-evidence-quote
      (head (V3ExpressionEvidence (quote ?expression)
              (V3HeldEvidence ?type ?card ?facts)))
      (body (V3ExpressionEvidence ?expression
              (V3RuntimeEvidence ?stage ?type ?card ?facts))))
    (rule v3-expression-evidence-eval
      (head (V3ExpressionEvidence (eval ?held)
              (V3RuntimeEvidence V3StageEvaluated
                ?type ?card ?facts)))
      (body (V3ExpressionEvidence ?held
              (V3HeldEvidence ?type ?card ?facts))))
    (rule v3-expression-evidence-superpose-two-empty
      (head (V3ExpressionEvidence V3SourceSuperposeTwoEmpty
              (V3RuntimeEvidence V3StageEvaluated
                V3EmptyResult V3Nondet (V3Facts V3No V3No V3No))))
      (body))
    (rule v3-expression-evidence-if-all-empty
      (head (V3ExpressionEvidence V3SourceIfAllEmpty
              (V3RuntimeEvidence V3StageEvaluated
                V3EmptyResult V3Semidet (V3Facts V3No V3No V3No))))
      (body))
    (rule v3-expression-evidence-case-all-empty
      (head (V3ExpressionEvidence V3SourceCaseAllEmpty
              (V3RuntimeEvidence V3StageEvaluated
                V3EmptyResult V3Semidet (V3Facts V3No V3No V3No))))
      (body))
    (rule v3-alias-evidence
      (head (V3AliasEvidence ?evidence ?evidence)) (body))

    ; Lexical variables are de Bruijn indices by this layer.  These six rules
    ; cover let/let* alias transport and shadowing without name comparison.
    (rule v3-binding-lookup-zero
      (head (V3EnvLookup
              (V3EnvCons ?evidence ?environment)
              V3IndexZero ?evidence))
      (body))
    (rule v3-binding-lookup-succ
      (head (V3EnvLookup
              (V3EnvCons ?evidence ?environment)
              (V3IndexSucc ?index) ?found))
      (body (V3EnvLookup ?environment ?index ?found)))
    (rule v3-binding-resolve-value
      (head (V3ResolveBindingSource
              ?environment (V3ValueSource ?evidence) ?evidence))
      (body))
    (rule v3-binding-resolve-alias
      (head (V3ResolveBindingSource
              ?environment (V3AliasSource ?index) ?evidence))
      (body (V3EnvLookup ?environment ?index ?evidence)))
    (rule v3-binding-apply-nil
      (head (V3ApplyBindings
              ?environment V3BindingsNil ?environment))
      (body))
    (rule v3-binding-apply-cons
      (head (V3ApplyBindings
              ?environment (V3BindingsCons ?source ?rest) ?result))
      (body (V3ResolveBindingSource
              ?environment ?source ?evidence)
            (V3ApplyBindings
              (V3EnvCons ?evidence ?environment) ?rest ?result)))
    (rule v3-let-evidence
      (head (V3LetEvidence
              (V3RuntimeEvidence ?value-stage ?value-result
                ?value-card ?value-facts)
              (V3RuntimeEvidence ?body-stage ?body-result
                ?body-card ?body-facts)
              (V3RuntimeEvidence V3StageEvaluated ?body-result
                ?card ?body-facts)))
      (body (V3CardSeq ?value-card ?body-card ?card)))

    (rule v3-branch-none-none
      (head (V3BranchEvidence V3NoEvidence V3NoEvidence
              (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Semidet
                (V3Facts V3No V3No V3No))))
      (body))
    (rule v3-branch-some-none
      (head (V3BranchEvidence
              (V3SomeEvidence
                (V3RuntimeEvidence ?stage ?type ?card ?facts))
              V3NoEvidence
              (V3RuntimeEvidence V3StageEvaluated
                ?type ?result-card ?facts)))
      (body (V3CardSeq ?card V3Semidet ?result-card)))
    (rule v3-branch-none-some
      (head (V3BranchEvidence
              V3NoEvidence
              (V3SomeEvidence
                (V3RuntimeEvidence ?stage ?type ?card ?facts))
              (V3RuntimeEvidence V3StageEvaluated
                ?type ?result-card ?facts)))
      (body (V3CardSeq V3Semidet ?card ?result-card)))
    (rule v3-branch-some-some
      (head (V3BranchEvidence
              (V3SomeEvidence
                (V3RuntimeEvidence ?left-stage (V3TypedResult ?left)
                  ?left-card ?left-facts))
              (V3SomeEvidence
                (V3RuntimeEvidence ?right-stage (V3TypedResult ?right)
                  ?right-card ?right-facts))
              (V3RuntimeEvidence V3StageEvaluated (V3TypedResult ?joined)
                ?card ?facts)))
      (body (V3JoinPrecise ?left ?right ?joined)
            (V3CardSeq ?left-card ?right-card ?card)
            (V3FactsMeet ?left-facts ?right-facts ?facts)))
    (rule v3-conditional-evidence
      (head (V3ConditionalEvidence
              (V3RuntimeEvidence ?condition-stage ?condition-type
                ?condition-card ?condition-facts)
              ?then ?else
              (V3RuntimeEvidence V3StageEvaluated
                ?type ?card ?facts)))
      (body (V3BranchEvidence ?then ?else
              (V3RuntimeEvidence V3StageEvaluated
                ?type ?branch-card ?facts))
            (V3CardSeq ?condition-card ?branch-card ?card)))

    (rule v3-selection-list
      (head (V3SelectionEvidence
              (V3RuntimeEvidence ?stage ?type ?input-card
                (V3Facts V3Yes ?bool ?expression))
              V3RequireProperList ?continuation-card ?card))
      (body (V3CardSeq ?input-card ?continuation-card ?card)))
    (rule v3-selection-bool
      (head (V3SelectionEvidence
              (V3RuntimeEvidence ?stage ?type ?input-card
                (V3Facts ?list V3Yes ?expression))
              V3RequireBool ?continuation-card ?card))
      (body (V3CardSeq ?input-card ?continuation-card ?card)))
    (rule v3-selection-expression
      (head (V3SelectionEvidence
              (V3RuntimeEvidence ?stage ?type ?input-card
                (V3Facts ?list ?bool V3Yes))
              V3RequireNonemptyExpression ?continuation-card ?card))
      (body (V3CardSeq ?input-card ?continuation-card ?card)))
    (rule v3-fold-empty
      (head (V3FoldEmptyEvidence
              (V3RuntimeEvidence ?stage ?type ?card ?facts)
              (V3RuntimeEvidence V3StageEvaluated ?type ?card ?facts)))
      (body))
    (rule v3-once-det
      (head (V3OnceCard V3Det V3Det)) (body))
    (rule v3-once-semidet
      (head (V3OnceCard V3Semidet V3Semidet)) (body))
    (rule v3-once-nondet
      (head (V3OnceCard V3Nondet V3Semidet)) (body))
    (rule v3-all-empty-superpose
      (head (V3AllEmptySuperposeEvidence
              (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Nondet
                (V3Facts V3No V3No V3No))))
      (body))

    ; NIK evidence outcomes keep ignorance separate from refutation.
    ; The native decision wrapper constructs refutations from failed ground
    ; shape/grade decisions; Horn absence alone cannot construct one.
    (rule v3-evidence-outcome-undetermined
      (head (V3EvidenceOutcome
              (V3RuntimeEvidence ?stage V3UnknownResult ?card ?facts)
              ?expected ?demand V3Undetermined))
      (body))
    (rule v3-evidence-outcome-empty-established
      (head (V3EvidenceOutcome
              (V3RuntimeEvidence ?stage V3EmptyResult ?card ?facts)
              ?expected ?demand V3Established))
      (body (V3ModeFits (V3Grade ?card) ?demand)))
    (rule v3-evidence-outcome-established
      (head (V3EvidenceOutcome
              (V3RuntimeEvidence ?stage (V3TypedResult ?actual) ?card ?facts)
              ?expected ?demand V3Established))
      (body (V3Consistent ?actual ?expected)
            (V3ModeFits (V3Grade ?card) ?demand)))

    ; Grounding licenses require compiled-stage facts.  There is no
    ; source/evaluated rule, by design.
    (rule v3-grounding-compiled
      (head (V3GroundingLicensed ?fact V3StageCompiled)) (body))

    ; --- the composed call judgment (Lean: checkCall,
    ;     checkCall_reject_has_witness, checkCall_unknown_permissive) ---
    (rule v3-call-accept
      (head (V3CallAccept (V3Arrow ?doms ?effect ?cod) ?args ?demand))
      (body (V3ConsistentArgs ?args ?doms)
            (V3ModeFits ?effect ?demand)))

    ; --- Exact/Gradual/Conflict seam ---
    ; Exactness is a total Boolean relation.  The existing V3FactMeet is
    ; Boolean conjunction, so nested unknowns propagate without a second
    ; handwritten truth table.  Named effects are intentionally orthogonal
    ; to gradual ignorance.
    (rule v3-exact-ty-unknown
      (head (V3ExactTy V3Unknown V3No)) (body))
    (rule v3-exact-ty-prim
      (head (V3ExactTy (V3Prim ?prim) V3Yes)) (body))
    (rule v3-exact-ty-list
      (head (V3ExactTy (V3List ?element) ?exact))
      (body (V3ExactTy ?element ?exact)))
    (rule v3-exact-ty-product
      (head (V3ExactTy (V3Product ?fields) ?exact))
      (body (V3ExactArgs ?fields ?exact)))
    (rule v3-exact-ty-arrow
      (head (V3ExactTy
              (V3Arrow ?domains ?mode ?codomain) ?exact))
      (body (V3ExactArgs ?domains ?domains-exact)
            (V3ExactTy ?codomain ?codomain-exact)
            (V3FactMeet ?domains-exact ?codomain-exact ?exact)))
    (rule v3-exact-ty-union
      (head (V3ExactTy (V3Union ?left ?right) ?exact))
      (body (V3ExactTy ?left ?left-exact)
            (V3ExactTy ?right ?right-exact)
            (V3FactMeet ?left-exact ?right-exact ?exact)))
    (rule v3-exact-ty-newtype
      (head (V3ExactTy (V3Newtype ?brand ?representation) ?exact))
      (body (V3ExactTy ?representation ?exact)))
    (rule v3-exact-args-nil
      (head (V3ExactArgs V3ArgsNil V3Yes)) (body))
    (rule v3-exact-args-cons
      (head (V3ExactArgs (V3ArgsCons ?head ?tail) ?exact))
      (body (V3ExactTy ?head ?head-exact)
            (V3ExactArgs ?tail ?tail-exact)
            (V3FactMeet ?head-exact ?tail-exact ?exact)))

    ; V3SeamKind consumes the normalized result of checkEvidence:
    ; outcome, result kind, and (for typed evidence) combined exactness.
    ; This keeps refutation construction in the decision layer and makes
    ; the seam a small, total classifier.  Only V3Exact authorizes an
    ; optimization license; legacy or gradual acceptance never does.
    (rule v3-seam-conflict
      (head (V3SeamKind
              V3Refuted ?result-kind ?exactness V3Conflict)) (body))
    (rule v3-seam-undetermined
      (head (V3SeamKind
              V3Undetermined ?result-kind ?exactness V3Gradual)) (body))
    (rule v3-seam-incomplete
      (head (V3SeamKind
              V3Incomplete ?result-kind ?exactness V3Gradual)) (body))
    (rule v3-seam-established-exact
      (head (V3SeamKind
              V3Established V3TypedEvidence V3Yes V3Exact)) (body))
    (rule v3-seam-established-inexact
      (head (V3SeamKind
              V3Established V3TypedEvidence V3No V3Gradual)) (body))
    (rule v3-seam-established-untyped
      (head (V3SeamKind
              V3Established V3UntypedEvidence ?exactness V3Gradual)) (body))
  ))
