; One-step reaching definitions as an ordered bag. Kill facts are
; observed separately; this program does not apply a set quotient.

(assign 1 x)
(assign 1 y)
(succ 1 2)
(succ 2 3)
(kill 2 x)

!(match &self (, (succ $src $node) (assign $src $var)) (rd $node $src $var))
!(match &self (kill $node $var) (killed $node $var))
