; Nondeterminism: superpose and collapse

; Basic superpose
!(assertEqualToResult (superpose (a b c)) (a b c))

; Superpose with single element
!(assertEqualToResult (superpose (only)) (only))

; Empty superpose
!(assertEqualToResult (superpose ()) ())

; Collapse collects all results
!(assertEqual (collapse (superpose (1 2 3))) (1 2 3))

; Nondeterministic equation + collapse
(= (coin) heads)
(= (coin) tails)
!(assertEqualToResult (coin) (heads tails))
!(assertEqual (collapse (coin)) (heads tails))

; Superpose of expressions
!(assertEqualToResult (superpose ((pair a) (pair b) (pair c))) ((pair a) (pair b) (pair c)))
