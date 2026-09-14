!(bind! &space-substitution (new-space))
!(add-atom &space-substitution (item alpha))
(= (space-substitution) &space-substitution)

!(test (match &space-substitution (item $value) $value) alpha)
!(test (match (space-substitution) (item $value) $value) alpha)
!(test
    (let $space (space-substitution)
         (match $space (item $value) $value))
    alpha)
!(test
    (foldall union-atom
             (match (space-substitution) (item $value) ($value))
             ())
    (alpha))
