; PeTTa extended profile: query and space algebra.

!(bind! &query-algebra (new-space))
!(add-atom &query-algebra (f a))
!(add-atom &query-algebra (f a))
!(add-atom &query-algebra (g b))
!(add-atom &query-algebra (tag a))
!(add-atom &query-algebra (tag b))

; Additive query choice preserves branch order and multiplicity.
!(match &query-algebra (| (f $x) (g $x)) (branch $x))
!(match &query-algebra (| (f $x) (f $x)) (duplicate $x))

; The zero query has no answers.
!(collapse (match &query-algebra (|) impossible))

; Product distributes over additive choice as an answer-bag equality.
!(match &query-algebra (, (tag $x) (| (f $x) (g $x))) (left-dist $x))
!(match &query-algebra (| (, (tag $x) (f $x)) (, (tag $x) (g $x))) (right-dist $x))

!(bind! &left-space (new-space))
!(add-atom &left-space (f a))
!(add-atom &left-space (f a))
!(add-atom &left-space (f b))
!(bind! &right-space (new-space))
!(add-atom &right-space (f a))
!(add-atom &right-space (f c))

; Space meet takes the minimum multiplicity; union adds multiplicities.
!(match (, &left-space &right-space) (f $x) (meet $x))
!(match (| &left-space &right-space) (f $x) (union $x))

; Named forms denote the same two space operations.
!(bind! &named-meet (space-intersection &left-space &right-space))
!(bind! &named-union (space-union &left-space &right-space))
!(match &named-meet (f $x) (named-meet $x))
!(match &named-union (f $x) (named-union $x))
