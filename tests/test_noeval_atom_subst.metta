; HE-backed stdlib metaprogramming helpers layered on quote/sealed semantics.
!(noeval (+ 1 2))
!(atom-subst 5 $v (eval (+ $v 1)))
