; PeTTa extended profile: query and space algebra.

!(bind! &query-algebra (new-state (new-space)))
!(add-atom (get-state &query-algebra) (f a))
!(add-atom (get-state &query-algebra) (f a))
!(add-atom (get-state &query-algebra) (g b))
!(add-atom (get-state &query-algebra) (tag a))
!(add-atom (get-state &query-algebra) (tag b))

; Additive query choice preserves branch order and multiplicity.
!(match (get-state &query-algebra) (| (f $x) (g $x)) (branch $x))
!(match (get-state &query-algebra) (| (f $x) (f $x)) (duplicate $x))

; The zero query has no answers.
!(collapse (match (get-state &query-algebra) (|) impossible))

; Product distributes over additive choice as an answer-bag equality.
!(match (get-state &query-algebra) (, (tag $x) (| (f $x) (g $x))) (left-dist $x))
!(match (get-state &query-algebra) (| (, (tag $x) (f $x)) (, (tag $x) (g $x))) (right-dist $x))

!(bind! &left-space (new-state (new-space)))
!(add-atom (get-state &left-space) (f a))
!(add-atom (get-state &left-space) (f a))
!(add-atom (get-state &left-space) (f b))
!(bind! &right-space (new-state (new-space)))
!(add-atom (get-state &right-space) (f a))
!(add-atom (get-state &right-space) (f c))

; Space meet takes the minimum multiplicity; union adds multiplicities.
!(match (, (get-state &left-space) (get-state &right-space)) (f $x) (meet $x))
!(match (| (get-state &left-space) (get-state &right-space)) (f $x) (union $x))

; Named forms denote the same two space operations.
!(bind! &named-meet (new-state (space-intersection (get-state &left-space) (get-state &right-space))))
!(bind! &named-union (new-state (space-union (get-state &left-space) (get-state &right-space))))
!(match (get-state &named-meet) (f $x) (named-meet $x))
!(match (get-state &named-union) (f $x) (named-union $x))

; The same queries over a named space, from a function's body, and over
; &self answer alike.
!(bind! &named-algebra (new-space))
!(add-atom &named-algebra (f a))
!(add-atom &named-algebra (f a))
!(add-atom &named-algebra (g b))
!(add-atom &named-algebra (tag a))
!(add-atom &named-algebra (tag b))
!(match &named-algebra (| (f $x) (g $x)) (named-branch $x))
!(match &named-algebra (, (tag $x) (| (f $x) (g $x))) (named-left-dist $x))
!(match &named-algebra (| (, (tag $x) (f $x)) (, (tag $x) (g $x))) (named-right-dist $x))
(= (left-dist-in-body) (match &named-algebra (, (tag $x) (| (f $x) (g $x))) (body-left-dist $x)))
!(left-dist-in-body)
(self-f a)
(self-f a)
(self-g b)
(self-tag a)
(self-tag b)
!(match &self (| (self-f $x) (self-g $x)) (self-branch $x))
!(match &self (, (self-tag $x) (| (self-f $x) (self-g $x))) (self-left-dist $x))
