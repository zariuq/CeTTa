; Type annotations and type-aware evaluation
(: myval Int)
(= (myval) 42)

!(assertEqual (myval) 42)

; Type annotation lookup via match
(: speed Number)
!(assertEqualToResult (match &self (: speed $t) $t) (Number))
