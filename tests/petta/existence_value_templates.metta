; Existence observers whose match template is a run-time value: the
; observation ignores a value template, so membership answers it; a
; template that is no value (the empty expression, Empty, an error) is
; the host's, which evaluates the observer whole.
(fact a)
(fact b)
(item 1)
(= (present-v $s $a) (if (== () (collapse (once (match $s $a $a)))) absent present))
(= (present-t $p $t) (if (== () (collapse (once (match &self $p $t)))) absent present))
(= (present-r $p $t) (if (== (collapse (once (match &self $p $t))) ()) absent present))
!(present-v &self (fact a))
!(present-v &self (fact c))
!(present-t (fact a) (fact a))
!(present-t (fact c) (fact c))
!(present-t (item 1) 7)
!(present-t (item 2) 7)
!(present-r (fact b) done)
!(present-r (fact z) done)
!(let $x (fact b) (present-t $x $x))
!(present-t (fact a) ())
!(present-t (fact a) Empty)
!(present-t (fact a) (Error (fact a) reason))
!(present-t (fact a) (empty))
