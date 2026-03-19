; HE conformance: superpose and collapse
; Tests nondeterministic branching and collection

; superpose: branch over elements
!(assertEqualToResult (superpose (a b c)) (a b c))

; superpose: empty returns Empty
!(assertEqualToResult (superpose ()) ())

; collapse: collect all results into expression
!(assertEqual (collapse (superpose (a b c))) (a b c))

; superpose + collapse round-trip
!(assertEqual (collapse (superpose (1 2 3))) (1 2 3))
