; A Vanilla generalisation problem, written by the forest's SWI front end: the
; background, the metarules, the signature, the metasubstitutions the
; constraints allow, the examples and the bound.
(m parent a b)
(m parent b c)
(m parent x y)
(metarule identity $v0 (m identity $v0 $v1) (m $v0 $v2 $v3) ((m $v1 $v2 $v3)))
(metarule inverse $v0 (m inverse $v0 $v1) (m $v0 $v2 $v3) ((m $v1 $v3 $v2)))
(= (forest:signature) (child))
(allowed (m identity child parent))
(allowed (m inverse child parent))
(= (forest:allowed $s) (if (== (collapse (match &self (allowed $s) $s)) ()) False True))
(= (task-examples) ((m child b a)))
(= (task-bound) (steps 2))
