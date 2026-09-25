; HE promotes integer/float pairs the way upstream Number::PartialEq does.
; 1.5 does not promote to 1. PeTTa keeps the kinds distinct.
; Tags keep each fact visible when a dialect collapses identical results.

(= (same $x $x) yes)

(n 1 a)
(n 1.0 b)
(n 2 c)

!(same 1 1.0)
!(same 1.0 1)
!(same 1 1.5)
!(match &self (n 1 $tag) (hit $tag))
!(match &self (n 1.0 $tag) (hit $tag))
!(match &self (n 2.0 $tag) (hit $tag))
!(match &self (n 2.0 c) (ground c))
!(match &self (n 1 a) (ground a))
!(match &self (n 1.5 a) (miss))
!(same 3 3)
