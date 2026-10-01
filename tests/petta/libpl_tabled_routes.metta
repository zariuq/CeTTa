!(callPredicate (Predicate (consult "tests/petta/fixtures/libpl_tabled_routes.pl")))
!(msort (collapse (let $_ (callPredicate (Predicate (tabled_boundary_reach a $x))) $x)))
!(let $witness (once (let $_ (callPredicate (Predicate (tabled_boundary_reach a $x))) $x))
    (if (== $witness b) valid-witness
        (if (== $witness c) valid-witness
            (if (== $witness d) valid-witness invalid-witness))))
!(msort (collapse (let $_ (callPredicate (Predicate (tabled_boundary_reach a $x))) $x)))
!(msort (collapse (let* (($_ (callPredicate (Predicate (tabled_boundary_number $x))))
    ($y (#+ $x 10))) ($x $y))))
!(catch (callPredicate (Predicate (tabled_boundary_error $x))))
!(msort (collapse (let $_ (callPredicate (Predicate (tabled_boundary_number $x))) $x)))
