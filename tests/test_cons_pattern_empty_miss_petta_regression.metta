; Cons-pattern misses should stay silent in Petta, not turn into decons errors.

(= (head (cons $h $t)) $h)

!(assertEqualToResult (head ()) ())
!(assertEqualToResult (head foo) ())
