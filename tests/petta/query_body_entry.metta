; A query whose root is a control form, and a collection's body, run on the
; compiled tier as the body of a nullary equation.  A binding made inside an
; alternative stays visible to the rest of the query; a case pattern whose
; variable is bound before the case tests equality, in a query and in an
; equation alike; a query naming state keeps its reads' shared variables.
!(let $v (hyperpose ((let $x 1 ok))) ($v $x))
!(let $r (hyperpose ((chain 3 $x $x))) (pair $r $x))
(= (q1) (let $v (hyperpose ((let $x 1 ok))) ($v $x)))
!(q1)

!(bind! &existence-space (new-space))
!(add-atom &existence-space (key a))
(= (bound-case) (let $v other (case (once (match &existence-space (key a) hit)) ((Empty absent) ($v present)))))
!(collapse (bound-case))
(= (bound-param $v) (case (once (match &existence-space (key a) hit)) ((Empty absent) ($v present))))
!(collapse (bound-param other))
!(collapse (let $v other (case (once (match &existence-space (key a) hit)) ((Empty absent) ($v present)))))
!(collapse (let $v hit (case (once (match &existence-space (key a) hit)) ((Empty absent) ($v present)))))

!(let $u (change-state! shared-state (g $z)) (let $a (get-state shared-state) (let $b (get-state shared-state) (let (g 1) $a $b))))
