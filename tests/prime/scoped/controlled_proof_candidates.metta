; An ordinary relation delivers retained proof candidates through the shared
; frontier. Native checking consumes the actual candidate, not its label.
; This does not make native inhabitation itself a shared-hub provider.
!(bind! &controlled-proof:theory (new-space))
!(add-atom &controlled-proof:theory (: A (u 0)))
!(add-atom &controlled-proof:theory (: a A))
!(add-atom &controlled-proof:theory (: b A))
!(add-atom &controlled-proof:theory (: f (-> (x : A) A)))
!(add-atom &controlled-proof:theory (: P (-> (x : A) (u 0))))
!(add-atom &controlled-proof:theory (: h (P a)))
!(add-atom &controlled-proof:theory
  (: next (-> (x : A) (e : (P x)) (P (f x)))))
!(add-atom &controlled-proof:theory (chaining-rule next))

!(bind! &controlled-proof:searched (new-space queue))
!(bind! &controlled-proof:malformed (new-space queue))
; The candidate is the actual result of dependent inhabitation search.
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
    (search-first-inhabitant &controlled-proof:theory (P (f a)) 2)
    (add-atom &controlled-proof:searched (candidate $proof)))
; Two equal occurrences must survive scheduling and bag observation.
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
    (search-first-inhabitant &controlled-proof:theory (P (f a)) 2)
    (add-atom &controlled-proof:searched (candidate $proof)))
; A supplied label or a plausible head cannot repair a wrong dependent input.
!(add-atom &controlled-proof:malformed (candidate (next b h)))

(= (controlled-proof:candidate)
   (match &controlled-proof:searched (candidate $proof) $proof))
(= (controlled-proof:candidate)
   (match &controlled-proof:malformed (candidate $proof) $proof))

!(controlled-proof:candidate)
!(let $proof (controlled-proof:candidate)
   (type:check &controlled-proof:theory $proof (P (f a))))
!(let $proof (controlled-proof:candidate)
   (type:check &controlled-proof:theory $proof (P (f b))))
; Bounded failure remains a rejection receipt, not a counter-certificate.
!(search-first-inhabitant &controlled-proof:theory (P (f a)) 0)
