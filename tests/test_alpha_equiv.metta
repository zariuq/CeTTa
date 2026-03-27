; HE-backed alpha-equivalence: bijective variable renaming preserves shape,
; but repeated-variable structure still matters.

!(=alpha (R $x $y) (R $a $b))
!(=alpha (R $x $x) (R $a $b))
!(=alpha (R $x (S $x)) (R $y (S $y)))
!(=alpha (R $x (S $x)) (R $y (S $z)))
