; Lazy selection in HE's extended profiles stops a producer with infinitely
; many answers at the selection's bound; eager selection does not end.  The
; program bounds its own evaluation depth, so where selection is eager the
; run stops at that bound instead of growing without one.

!(pragma! select-mode lazy)
!(pragma! max-stack-depth 2000)

; Each answer writes before it is given.
(= (enats $tag $n) (let $u (add-atom &self (made $tag $n)) $n))
(= (enats $tag $n) (enats $tag (+ $n 1)))
!(select 2 (enats two 0))
!(collapse (match &self (made two $x) $x))
!(once (enats one 0))
!(collapse (match &self (made one $x) $x))

; A pure producer, whose answers a compiled answer producer gives: it gives
; the selection only the answers it asks for.
(= (nats $n) $n)
(= (nats $n) (nats (+ $n 1)))
!(select 3 (nats 0))
!(once (nats 5))
