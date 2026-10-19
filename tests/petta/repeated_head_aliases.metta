; A head that repeats a variable unifies the arguments it meets there, aliases
; included: (G $a $b) against (G $b c) binds $b to $a and then both to c.
; Each answer shows the bindings the match made.
(= (same $x $x) yes)
(= (same3 $x $x $x) yes)
!(collapse (let $r (same (G $a $b) (G $b c)) ($r $a $b)))
!(collapse (let $r (same (G $b c) (G $a $b)) ($r $a $b)))
!(collapse (let $r (same (G $a $b c) (G $b $c $c)) ($r $a $b $c)))
!(collapse (let $r (same3 $p (G $a $b) (G $b c)) ($r $p $a $b)))
!(collapse (let $k (G $b c) (let $r (same (G $a $b) $k) ($r $a $b))))
!(collapse (let $r (same (G $a $b) (G $b $a)) (let $a q ($r $a $b))))
!(collapse (same (G $a b) (G $a c)))
