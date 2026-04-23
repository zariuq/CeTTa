!(test (progn (translatePredicate (is $x 2))
              (translatePredicate (+ $x 40 $z))
              $z)
       42)

!(test (progn (translatePredicate (= $who sam))
              $who)
       sam)

!(test (progn (translatePredicate (< 2 3 $ok))
              $ok)
       True)
