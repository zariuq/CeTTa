; HE conformance: superpose, canonical reify, and compatibility collapse
; Tests nondeterministic branching and collection

; superpose: branch over elements
!(assertEqualToResult (superpose (a b c)) (a b c))

; superpose: empty returns no user-visible results
!(assertEqualToResult (superpose ()) ())

; collapse: collect all results into expression
!(assertEqual (collapse (superpose (a b c))) (a b c))

; reify: canonical cross-dialect occurrence materializer
!(assertEqual (reify (superpose (a a b))) (a a b))

; superpose + collapse round-trip
!(assertEqual (collapse (superpose (1 2 3))) (1 2 3))
