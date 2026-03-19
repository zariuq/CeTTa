; HE conformance: match
; Tests pattern matching against space equations

(= (color) red)
(= (color) green)

; match with pattern variable — should find both results
!(assertEqualToResult (match &self (= (color) $x) $x) (red green))

; match with no matching equation — should return Empty
!(assertEqualToResult (match &self (= (missing) $x) $x) ())

; match with expression pattern
(= (pair A) (result-A))
!(assertEqual (match &self (= (pair A) $r) $r) (result-A))
