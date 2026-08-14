!(prolog:query (quote true) (quote absent))
!(import! &self lib_prolog)
!(prolog:available?)
!(prolog:query (quote (between 1 3 $x)) (quote $x))
!(prolog:query (quote (member $x (a a b))) (quote $x))
!(prolog:query
    (quote
        (member
            (prolog:compound (pair $x $y))
            ((prolog:compound (pair a 1))
             (prolog:compound (pair b 2)))))
    (quote (prolog:compound (answer $x $y))))
!(prolog:query (quote (member z (a b))) (quote yes))
!(prolog:query
    (quote (cetta_lib_prolog_missing 1))
    (quote impossible))
!(prolog:query
    (quote (flag cetta_lib_prolog_syntax_effect $old 0))
    (quote reset))
!(prolog:query
    (quote
        (flag
            cetta_lib_prolog_syntax_effect
            $old
            (prolog:compound (+ $old 1))))
    (quote $old))
!(prolog:query
    (quote
        (flag
            cetta_lib_prolog_syntax_effect
            $old
            (prolog:compound (+ $old 1))))
    (quote $old))
