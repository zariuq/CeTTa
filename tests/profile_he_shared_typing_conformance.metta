; Shared HE typing canaries derived from the pinned upstream Rust behavior.
; Run under he, he-compat, he-extended, and he-prime.
(: SharedR Type)
(: SharedA Type)
(: SharedB Type)
(: shared-a SharedA)
(: shared-b SharedB)

; Dependent codomain substitution.
(: SharedEq (-> $t $t Type))
(: SharedRefl (-> $x (SharedEq $x $x)))
(: shared-term SharedA)

; Atom is top only in expected position.
(: shared-a-fn (-> SharedA SharedR))
(: shared-atom-fn (-> Atom SharedR))
(: shared-atom-result (-> SharedA Atom))
(: shared-expr-fn (-> Expression SharedR))
(: shared-expr-result (-> SharedA Expression))

; Each nested %Undefined% is an independent gradual wildcard.
(: SharedWrap (-> Type Type))
(: shared-wrapped-a (SharedWrap SharedA))
(: shared-dynamic-fn (-> (SharedWrap %Undefined%) SharedR))
(: shared-dynamic-wrapped (SharedWrap %Undefined%))
(: shared-exact-fn (-> (SharedWrap SharedA) SharedR))

!(assertEqual (get-type (SharedRefl shared-term))
              (SharedEq SharedA SharedA))
!(assertEqual (get-type (shared-a-fn shared-a)) SharedR)
!(assertEqualToResult (get-type (shared-a-fn shared-b)) ())
!(assertEqual (get-type (shared-atom-fn shared-a)) SharedR)
!(assertEqual (get-type (shared-atom-fn ())) SharedR)
!(assertEqualToResult
    (get-type (shared-a-fn (shared-atom-result shared-a))) ())
!(assertEqual
    (get-type (shared-atom-fn (shared-atom-result shared-a))) SharedR)
!(assertEqual
    (get-type (shared-expr-fn (shared-atom-result shared-a))) SharedR)
!(assertEqual
    (get-type (shared-expr-fn (shared-expr-result shared-a))) SharedR)
!(assertEqual (get-type (shared-dynamic-fn shared-wrapped-a)) SharedR)
!(assertEqual (get-type (shared-exact-fn shared-dynamic-wrapped)) SharedR)
