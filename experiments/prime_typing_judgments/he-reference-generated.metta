; Generated from HE.TypeRuleExamples; run with the HE reference dialect.
; Declaration occurrences and independent annotation scopes are observable.
!(bind! &he-reference (new-space))
!(add-atom &he-reference (: x Number))
!(add-atom &he-reference (: x Number))
!(assertEqual (collapse (get-type x &he-reference)) (Number Number))
!(assertEqual (collapse (get-type missing &he-reference)) (%Undefined%))
!(assertEqual (collapse (get-type 7 &he-reference)) (Number))
!(bind! &he-application (new-space))
!(add-atom &he-application (: g (-> $t A R)))
!(add-atom &he-application (: b B))
!(add-atom &he-application (: k $t))
!(assertEqual (collapse (get-type (g b k) &he-application)) (R))
