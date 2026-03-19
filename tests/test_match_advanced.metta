; Advanced match operations

; Match against type annotations
(: foo Int)
(: bar Bool)
!(assertEqualToResult (match &self (: foo $t) $t) (Int))
!(assertEqualToResult (match &self (: bar $t) $t) (Bool))

; Match against equations
(= (color) red)
(= (color) green)
(= (color) blue)
!(assertEqualToResult (match &self (= (color) $c) $c) (red green blue))

; Match with nested pattern
(= (pair a b) result-ab)
!(assertEqualToResult (match &self (= (pair $x $y) $r) ($x $y $r)) ((a b result-ab)))

; Match with no results — empty
!(assertEqualToResult (match &self (= (nonexistent) $x) $x) ())

; Match against plain atoms in space
(fact earth planet)
(fact mars planet)
(fact sun star)
!(assertEqualToResult (match &self (fact $name planet) $name) (earth mars))
!(assertEqualToResult (match &self (fact $name star) $name) (sun))
