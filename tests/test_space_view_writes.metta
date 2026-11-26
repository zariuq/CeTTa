; A space expression of the read algebra -- (, A B) the meet of its spaces,
; (| A B) their additive union -- is a read-only view: matching it reads the
; spaces, and a write to it is refused as a write to any operand that is no
; space is, with the spaces unchanged.
!(bind! &a (new-space))
!(bind! &b (new-space))
!(add-atom &a (x 1))
!(add-atom &b (x 1))
!(add-atom &b (y 2))
(= (put $s $x) (add-atom $s $x))
; Positive controls: writes to the spaces themselves land, through a
; function too.
!(add-atom &a (w 0))
!(remove-atom &a (w 0))
!(put &a (v 9))
!(remove-atom &a (v 9))
!(collapse (get-atoms &a))
; The views read the spaces.
!(collapse (match (, &a &b) $p $p))
!(collapse (match (| &a &b) (x $v) $v))
; Negative controls: an operand that is no space.
!(case (add-atom (foo bar) (z 3)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
; Writes to views: flat, nested, a singleton fold, bound by let, through a
; function, and in bulk.
!(case (add-atom (, &a &b) (z 3)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (remove-atom (, &a &b) (x 1)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (add-atom (| &a &b) (z 3)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (remove-atom (| &a &b) (y 2)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (add-atom (, (| &a &b) &a) (z 3)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (add-atom (, &a) (z 3)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (let $v (, &a &b) (add-atom $v (q 5))) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (put (, &a &b) (z 3)) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
!(case (add-atoms (, &a &b) ((u 1) (u 2))) (((Error $call $why) (Refused $why)) ($other (Accepted $other))))
; The spaces are as they were.
!(collapse (get-atoms &a))
!(collapse (get-atoms &b))
