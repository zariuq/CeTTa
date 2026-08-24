; Native regular-lambda-Pi formation establishes the type before the distinct
; space-indexed HE refinement service is consulted.
!(type:refine (-> u0 u0))

(: provisional-true-refinement (-> Atom Bool))
(= (provisional-true-refinement $type) True)
(type-level-function provisional-true-refinement)
(type-index-refinement -> 1 provisional-true-refinement)
!(type:refine (-> u0 u0))

; A marker alone cannot admit a user computation into Prime, regardless of
; whether its HE result would be true or false.
(: provisional-false-refinement (-> Atom Bool))
(= (provisional-false-refinement $type) False)
(type-level-function provisional-false-refinement)
(type-index-refinement -> 1 provisional-false-refinement)
!(type:refine (-> u0 u0))

; u1 is the distinguished formed top sort; it has no higher synthesized sort.
!(type:refine u1)
