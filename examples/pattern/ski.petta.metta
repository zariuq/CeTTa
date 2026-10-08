; PeTTa evaluates compound operators differently from HE. A named function
; exposes a reduction step over SKI syntax without changing that contract.
(=% (ski (I $x)) $x)
(=% (ski ((K $x) $y)) $x)
(=% (ski (((S $x) $y) $z)) (($x $z) ($y $z)))
(=% (ski ((S K) K)) I)

!(ski ((S K) K))
!(ski ((K a) b))
; A known PeTTa function with no applicable clause has no answer.
!(collapse (ski ((S K) $unknown)))
(= (relational K) selected)
!(let $answer (relational $caller) (answer $answer $caller))
