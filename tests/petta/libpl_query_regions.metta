!(import! &self lib_prolog)
!(size-atom (prolog:query
    (quote (, (prolog:compound (between 1 10000 $i))
        (prolog:compound (= $v (nested $i (a b c))))))
    (quote $v)))
!(size-atom (prolog:query
    (quote (, (prolog:compound (between 1 10000 $i))
        (prolog:compound (= $v (pair $x $x)))))
    (quote $v)))
