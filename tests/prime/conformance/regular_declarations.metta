; Exact closed declarations are native Prime assumptions.  The source space
; remains authoritative; the derived declaration context is only a
; revision-current realization used during one judgment.

!(bind! &decl-exact (new-space))
!(add-atom &decl-exact (: a (-> u0 u0)))
!(add-atom &decl-exact (: ab (-> (-> u0 u0) (-> u0 (-> u0 u0)))))

!(type:of &decl-exact (ab a))
!(type:check &decl-exact (ab a) (-> u0 (-> u0 u0)))
!(type:check &decl-exact (a a) u0)
!(type:eq &decl-exact a a)
!(type:eq &decl-exact (ab a) (ab a))

; Identical duplicate declarations preserve the same assumption.
!(add-atom &decl-exact (: a (-> u0 u0)))
!(type:of &decl-exact (ab a))

; A declaration type may depend on earlier small values when the resulting
; context is already admitted by the sealed calculus.  Synthesis quotes the
; intrinsic indices back to the authored declaration names.
!(bind! &decl-dependent (new-space))
!(add-atom &decl-dependent (: p u0))
!(add-atom &decl-dependent (: h (id u0 p p)))
!(add-atom &decl-dependent (: q u0))
!(type:formed &decl-dependent (id u0 p p))
!(type:of &decl-dependent h)
!(type:check &decl-dependent h (id u0 p p))
!(type:check &decl-dependent h (id u0 q q))
!(type:eq &decl-dependent h h)
; Constructed evidence uses the same declaration context as assumed evidence.
; The declaration-aware recognizer must run before the context-free authored
; recognizer, which cannot interpret the free declaration name `p`.
!(type:of &decl-dependent (refl p))
!(type:check &decl-dependent (refl p) (id u0 p p))
!(type:analyze &decl-dependent (refl p) (id u0 p p))
!(type:eq &decl-dependent (refl p) (refl p))

; Variables-as-types and unresolved declaration dependencies are not silently
; promoted into this class.  They retain the ambient MeTTa interpretation.
!(bind! &decl-tower (new-space))
!(add-atom &decl-tower (: Ty u1))
!(add-atom &decl-tower (: x Ty))
!(type:of &decl-tower x)

!(bind! &decl-unresolved (new-space))
!(add-atom &decl-unresolved (: broken (id u0 missing missing)))
!(type:of &decl-unresolved broken)

!(bind! &decl-cycle (new-space))
!(add-atom &decl-cycle
  (: cycle-left (id u0 cycle-right cycle-right)))
!(add-atom &decl-cycle
  (: cycle-right (id u0 cycle-left cycle-left)))
!(type:of &decl-cycle cycle-left)

; Conflicts and HE-style open schemes never mint native declaration evidence.
!(bind! &decl-conflict (new-space))
!(add-atom &decl-conflict (: c u0))
!(add-atom &decl-conflict (: c (-> u0 u0)))
!(type:of &decl-conflict c)
!(type:eq &decl-conflict c c)

!(bind! &decl-open (new-space))
!(add-atom &decl-open (: poly (-> $t $t)))
!(type:of &decl-open poly)

; An unknown constant remains with the ambient semantics.
!(type:of &decl-exact missing)

; Producer-bound aggregate judgments preserve the same declaration context
; for constructed dependent evidence.  The producer is demanded once; its
; returned proof is checked by the declaration-aware authority.
(: producer-point u0)
(= (producer-dependent-proof) (refl producer-point))
!(type:may (producer-dependent-proof) (id u0 producer-point producer-point))
!(type:must (producer-dependent-proof) (id u0 producer-point producer-point))

; A variable scoped only by `(u $level)` in a declaration signature is a
; universe-schema parameter.  Each native judgment chooses a closed tower
; instance, then the ordinary intrinsic kernel replays the chosen instance.
!(bind! &decl-level-poly (new-space))
!(add-atom &decl-level-poly
  (: poly-id (-> (A : (u $level)) (-> A A))))
!(type:of &decl-level-poly (poly-id u0))
!(type:of &decl-level-poly (poly-id u1))
!(type:of &decl-level-poly (poly-id u1 u0))
!(type:check &decl-level-poly (poly-id u1) (-> u1 u1))
!(type:check &decl-level-poly
  (pair (poly-id u0) (poly-id u1))
  (sigma (_ : (-> u0 u0)) (-> u1 u1)))
; Distinct schema parameters remain independent within one declaration.
!(add-atom &decl-level-poly
  (: poly-choose
    (-> (A : (u $left-level))
        (B : (u $right-level))
        (-> A (-> B A)))))
!(type:of &decl-level-poly (poly-choose u0 u1))
!(type:check &decl-level-poly (poly-id u1) (-> u0 u0))

; Polymorphic declarations may depend on other polymorphic declarations.
; This is the public, lowercase Prime surface used by the native typed-flow
; client: `hyp` and its rules remain authored language declarations, while
; dependent application constructs their indexed judgments.  `sort-code`
; inhabits an explicit tower universe: legacy `u0` is a ground type and is
; deliberately not confused with the tower's `(u 0)`.
!(bind! &decl-hyp (new-space))
!(add-atom &decl-hyp
  (: hyp
    (-> (sorts : (u $sort-level))
        (primitives :
          (-> sorts (-> sorts (u $primitive-level))))
        (source-sort : sorts) (target-sort : sorts)
        (u $hypothesis-level))))
!(add-atom &decl-hyp
  (: hyp:primitive
    (-> (sorts : (u $sort-level))
        (primitives :
          (-> sorts (-> sorts (u $primitive-level))))
        (source-sort : sorts) (target-sort : sorts)
        (symbol : (primitives source-sort target-sort))
        (hyp sorts primitives source-sort target-sort))))
!(add-atom &decl-hyp
  (: hyp:chain
    (-> (sorts : (u $sort-level))
        (primitives :
          (-> sorts (-> sorts (u $primitive-level))))
        (source-sort : sorts) (middle-sort : sorts)
        (target-sort : sorts)
        (earlier :
          (hyp sorts primitives source-sort middle-sort))
        (later :
          (hyp sorts primitives middle-sort target-sort))
        (hyp sorts primitives source-sort target-sort))))
!(add-atom &decl-hyp (: sort-code (u 0)))
!(add-atom &decl-hyp (: person-sort sort-code))
!(add-atom &decl-hyp (: number-sort sort-code))
!(add-atom &decl-hyp
  (: primitive-vocabulary
    (-> sort-code (-> sort-code (u 1)))))
!(add-atom &decl-hyp
  (: alternate-primitive-vocabulary
    (-> sort-code (-> sort-code (u 1)))))
!(add-atom &decl-hyp
  (: mother-symbol
    (primitive-vocabulary person-sort person-sort)))
!(add-atom &decl-hyp
  (: father-symbol
    (primitive-vocabulary person-sort person-sort)))
!(add-atom &decl-hyp
  (: successor-symbol
    (primitive-vocabulary number-sort number-sort)))
!(add-atom &decl-hyp
  (: alternate-father-symbol
    (alternate-primitive-vocabulary person-sort person-sort)))
!(type:of &decl-hyp
  (hyp:primitive sort-code primitive-vocabulary
    person-sort person-sort mother-symbol))
!(type:of &decl-hyp
  (hyp:chain sort-code primitive-vocabulary
    person-sort person-sort person-sort
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort mother-symbol)
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort father-symbol)))
!(type:of &decl-hyp
  (hyp:chain sort-code primitive-vocabulary
    person-sort person-sort person-sort
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort mother-symbol)
    (hyp:primitive sort-code primitive-vocabulary
      number-sort number-sort successor-symbol)))
!(type:of &decl-hyp
  (hyp:chain sort-code primitive-vocabulary
    person-sort person-sort person-sort
    (hyp:primitive sort-code primitive-vocabulary
      person-sort person-sort mother-symbol)
    (hyp:primitive sort-code alternate-primitive-vocabulary
      person-sort person-sort alternate-father-symbol)))

; Duplicate schemas compare modulo their authored level-variable identity.
!(add-atom &decl-level-poly
  (: poly-id (-> (B : (u $other-level)) (-> B B))))
!(type:of &decl-level-poly (poly-id u1))
