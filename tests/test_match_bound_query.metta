; An earlier match supplies the next query, including its connective head.
; Nested match planning must wait until that outer shape is known.
!(bind! &bound-facts (new-space))
!(bind! &bound-rules (new-space))
!(add-atom &bound-facts (edge a b))
!(add-atom &bound-facts (edge b c))
!(add-atom &bound-facts (path a b))
!(add-atom &bound-rules (query join (, (path $x $y) (edge $y $z)) (path $x $z)))
!(add-atom &bound-rules (query atomic (edge $x $y) (pair $x $y)))
!(add-atom &bound-rules (head join ,))
!(add-atom &bound-rules (head atomic edge))
!(add-atom &bound-rules (query unit (,) unit))
!(add-atom &bound-rules (query absent (, (path $x $y) (edge $y nowhere)) $x))

!(assertEqualToResult
    (match &bound-rules (query join $q $o) (match &bound-facts $q $o))
    ((path a c)))
!(assertEqualToResult
    (match &bound-rules (head join $h)
        (match &bound-facts ($h (path $x $y) (edge $y $z)) (path $x $z)))
    ((path a c)))
!(assertEqualToResult
    (match &bound-rules (query atomic $q $o) (match &bound-facts $q $o))
    ((pair a b) (pair b c)))
!(assertEqualToResult
    (match &bound-rules (head atomic $h) (match &bound-facts ($h $x $y) (pair $x $y)))
    ((pair a b) (pair b c)))
!(assertEqualToResult
    (match &bound-rules (query atomic (edge $x $y) $o)
        (match &bound-facts (edge $x $y) $o))
    ((pair a b) (pair b c)))
!(assertEqualToResult
    (match &bound-rules (query unit $q $o) (match &bound-facts $q $o))
    (unit))
!(assertEqualToResult
    (match &bound-rules (query absent $q $o) (match &bound-facts $q $o))
    ())

; The same boundary inside an equation body and a collected result.
(= (bound-plan $facts $rules)
   (match $rules (query join $q $o) (match $facts $q $o)))
!(assertEqualToResult (bound-plan &bound-facts &bound-rules) ((path a c)))
!(assertEqual (collapse (bound-plan &bound-facts &bound-rules)) ((path a c)))

; Every duplicate occurrence must survive planning.
!(add-atom &bound-facts (edge b c))
!(assertEqualToResult (bound-plan &bound-facts &bound-rules) ((path a c) (path a c)))
!(add-atom &bound-rules (query join (, (path $x $y) (edge $y $z)) (path $x $z)))
!(assertEqualToResult (bound-plan &bound-facts &bound-rules)
    ((path a c) (path a c) (path a c) (path a c)))

; Effectful templates run once for each answer and keep its bindings.
!(bind! &bound-seen (new-space))
!(assertEqualToResult
    (match &bound-rules (query join $q $o)
        (match &bound-facts $q (let $done (add-atom &bound-seen $o) $o)))
    ((path a c) (path a c) (path a c) (path a c)))
!(assertEqualToResult (match &bound-seen (path $x $y) (path $x $y))
    ((path a c) (path a c) (path a c) (path a c)))
