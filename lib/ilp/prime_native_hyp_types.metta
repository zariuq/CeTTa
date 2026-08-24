; Prime typing presentation for the proof-relevant indexed family `hyp`.
; It is separate from operational interpretation so a language can attach the
; profile without changing how raw programs execute.

(: hyp
  (-> (sorts : (u $sort-level))
      (primitives :
        (-> sorts (-> sorts (u $primitive-level))))
      (source-sort : sorts) (target-sort : sorts)
      (u $hypothesis-level)))

(: hyp:primitive
  (-> (sorts : (u $sort-level))
      (primitives :
        (-> sorts (-> sorts (u $primitive-level))))
      (source-sort : sorts) (target-sort : sorts)
      (symbol : (primitives source-sort target-sort))
      (hyp sorts primitives source-sort target-sort)))

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
      (hyp sorts primitives source-sort target-sort)))
