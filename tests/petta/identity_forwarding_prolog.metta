; Identity forms: (chain Source $v $v), binder operand 2, source operand 1;
; (let $v Source $v), binder operand 1, source operand 2.  SWI-Prolog's own
; delayed goals on the binder wake through forwarding, and an exception a
; Prolog goal throws keeps its enclosing handler.
!(collapse (let $u (translatePredicate (dif $x 1)) (chain 1 $x $x)))
!(let $u (translatePredicate (freeze $x (Predicate (writeln woke)))) (let $d (chain (+ 0 1) $x $x) $d))
; Forwarding preserves an escaped exception's enclosing handler.
!(catch (chain (callPredicate (Predicate (throw identity-forward-fault))) $v $v))
!(catch (let $v (callPredicate (Predicate (throw identity-forward-fault))) $v))
