; Deep unify coverage — repeated vars, nested patterns
!(assertEqual (unify ($x $x) (a a) same diff) same)
!(assertEqual (unify ($x $x) (a b) same diff) diff)
!(assertEqual (unify (f (g $x)) (f (g 42)) $x fail) 42)
!(assertEqual (unify (f $x $y $x) (f a b a) (ok $x $y) fail) (ok a b))
!(assertEqual (unify (f $x $y $x) (f a b c) ok fail) fail)
