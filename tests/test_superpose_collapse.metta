; HE conformance: superpose and collapse
; Tests nondeterministic branching and collection

; superpose: branch over elements
!(assertEqualToResult (superpose (a b c)) (a b c))

; superpose: empty returns no user-visible results
!(assertEqualToResult (superpose ()) ())

; Empty is an internal no-result sentinel, not a user-visible branch.
!(assertEqualToResult (superpose (a Empty b)) (a b))

; collapse: collect all results into expression
!(assertEqual (collapse (superpose (a b c))) (a b c))

; collapse must preserve the same Empty filtering.
!(assertEqual (collapse (superpose (a Empty b))) (a b))

; superpose + collapse round-trip
!(assertEqual (collapse (superpose (1 2 3))) (1 2 3))
