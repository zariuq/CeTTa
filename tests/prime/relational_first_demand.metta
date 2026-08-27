; Bounded observation pulls one root answer without truncating nested search.

!(bind! &prime-pull:two-space (new-space queue))
!(add-atom &prime-pull:two-space (answer first))
!(add-atom &prime-pull:two-space (answer second))

!(bind! &prime-pull:empty-space (new-space queue))
!(bind! &prime-pull:later-space (new-space queue))
!(add-atom &prime-pull:later-space (answer later))

!(bind! &prime-pull:nested-space (new-space queue))
!(add-atom &prime-pull:nested-space (answer rejected))
!(add-atom &prime-pull:nested-space (answer wanted))

; A later answer in the relation machine must remain outside the bounded
; observation.
(= (prime-pull:from-space $token)
   (match &prime-pull:two-space (answer $value) $value))
!(once (prime-pull:from-space token))

; A readout around unknown data remains ordinary Prime data.  The admission
; rule is not permission to reinterpret an unknown body as a relation.
!(once (prime-pull:unknown token))

; An empty earlier equation does not consume the first-answer demand.
(= (prime-pull:empty-or-later $token)
   (match &prime-pull:empty-space (answer $value) $value))
(= (prime-pull:empty-or-later $token)
   (match &prime-pull:later-space (answer $value) $value))
!(once (prime-pull:empty-or-later token))

; Without a bounded observer, all relation answers remain visible.
!(prime-pull:from-space token)
!(collapse (prime-pull:from-space token))

; A root-level bound must not truncate the nested relation before its second
; candidate can satisfy the enclosing relation.
(= (prime-pull:inner $token)
   (match &prime-pull:nested-space (answer $value) $value))
(= (prime-pull:nested $token)
   (let $candidate (prime-pull:inner $token)
     (if (== $candidate wanted)
         accepted
         (empty))))
!(once (prime-pull:nested token))
