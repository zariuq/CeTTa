!(test (collapse (match &missing-space (item $value) $value)) ())
!(test (collapse (match not-a-space (item $value) $value)) ())
(= (missing-space) &missing-space)
!(test
    (foldall +
             (match &missing-space (item $value) 1)
             0)
    0)
!(test
    (foldall +
             (match (missing-space) (item $value) 1)
             0)
    0)

!(add-atom &left-space (item alpha))
!(add-atom &left-space (item alpha))
!(add-atom &left-space (item beta))
!(add-atom &right-space (item gamma))

!(test
    (collapse (match &left-space (item $value) $value))
    (alpha alpha beta))
!(test
    (collapse (match &right-space (item $value) $value))
    (gamma))

(= (left-space) &left-space)
!(test
    (let $space (left-space)
         (collapse (match $space (item $value) $value)))
    (alpha alpha beta))
!(test
    (foldall +
             (match (left-space) (item $value) 1)
             0)
    3)

!(remove-atom &left-space (item alpha))
!(test
    (collapse (match &left-space (item $value) $value))
    (beta))
!(test (is-space &left-space) True)
!(test (is-space (left-space)) True)
!(test (is-space not-a-space) False)
