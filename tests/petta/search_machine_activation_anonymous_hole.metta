(= (hole-source) alpha)
(= (hole-source) beta)
(= (hole-source) alpha)
(= (hole-empty) (empty))

; An anonymous binding contributes no user-addressable substitution, but the
; open producer's authored order and duplicate occurrences remain observable.
(= (discard-many)
   (let $_ (hole-source) kept))
(= (discard-empty)
   (let $_ (hole-empty) unreachable))

; Named and structured binders remain on the canonical matching path.
(= (keep-named)
   (let $value (hole-source) $value))
(= (pattern-source) (tag one))
(= (pattern-source) not-a-tag)
(= (keep-structured)
   (let (tag $value) (pattern-source) $value))

; An effectful producer is not granted a local-body certificate merely by an
; anonymous consumer.  Canonical fallback must still execute the effect once.
!(bind! anonymous-hole-state (new-state 0))
(= (effect-source)
   (change-state! anonymous-hole-state
     (+ (get-state anonymous-hole-state) 1)))
(= (discard-effect)
   (let $_ (effect-source) effect-kept))

!(discard-many)
!(discard-empty)
!(keep-named)
!(keep-structured)
!(discard-effect)
!(get-state anonymous-hole-state)
