!(bind! &foo (new-space))
!(add-atom &foo (= (helper) local))
!(add-atom &foo (= (outer) (helper)))

; Explicit context applies to the complete nested call.
!(eval (outer) &foo)

; Matching a space does not implicitly enter it; eval does so explicitly.
!(match &foo (= (outer) $body) (eval $body &foo))

; The default context remains isolated.
!(eval (outer))
