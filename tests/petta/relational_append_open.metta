; The first list and output are unknown at append entry. Later constraints
; require progressively longer prefixes; once validates a witness, not an order.
!(once (let $r (append $x (b)) (let (b) $r (== $x ()))))
!(once (let $r (append $x (b)) (let (a b) $r (== $x (a)))))
!(once (let $r (append $x (b)) (let (a c b) $r (== $x (a c)))))
; Known output retains finite splitting and respects repeated variables.
!(collapse (let (a b) (append $x (b)) $x))
!(collapse (let (a b) (append $x $x) $x))
; A non-list output allows the empty prefix only.
!(once (let $r (append $x tail) (let tail $r (== $x ()))))
