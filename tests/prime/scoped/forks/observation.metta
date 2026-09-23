; FORK-OBS: what does a query return — distinct values, counted results,
; or occurrences that carry their derivation?

!(bind! &kb (new-space))
!(add-atom &kb (edge a b))
!(add-atom &kb (edge b c))
!(add-atom &kb (edge a c))

; A path relation whose answers carry their route.
(= (path $x $y) (match &kb (edge $x $y) (direct $x $y)))
(= (path $x $y) (match &kb (edge $x $z) (let $rest (path $z $y) (via $z $rest))))

; Occurrence view: two derivations of "c is reachable from a", each with its route.
!(path a c)
; Counted view: the same query, routes forgotten, multiplicity kept.
(= (reachable $x $y) (let $_ (path $x $y) True))
!(reachable a c)
; Distinct-value view: multiplicity forgotten too.
!(unique (reachable a c))
; Only the occurrence view can answer "do the two derivations share a premise?"
(= (uses-edge (direct $x $y) $u $v) (and (== $x $u) (== $y $v)))
(= (uses-edge (via $z $rest) $u $v) (uses-edge $rest $u $v))
!(let $route (path a c) (Pair $route (collapse (uses-edge $route b c))))
