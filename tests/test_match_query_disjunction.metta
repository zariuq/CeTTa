; Pattern-level pipe concatenates answer bags in declaration order while each
; disjunct starts from the same incoming bindings.  Repeating a disjunct keeps
; multiplicity; the nullary form is zero; variables absent from the winning
; branch remain unsubstituted in the template.
!(bind! &query-disjunction (new-space))
!(add-atom &query-disjunction (f a))
!(add-atom &query-disjunction (f a))
!(add-atom &query-disjunction (g b))
!(add-atom &query-disjunction (left l))
!(add-atom &query-disjunction (right r))
!(match &query-disjunction (| (f $x) (g $x)) $x)
!(match &query-disjunction (| (f $x) (f $x)) (duplicate $x))
!(match &query-disjunction (|) impossible)
!(match &query-disjunction (| (left $x) (right $y)) (local $x $y))
!(collapse (match &query-disjunction (| (f $x) (g $x)) (collapsed $x)))
