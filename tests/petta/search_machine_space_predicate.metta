(: succeedsPredicate (-> Atom Bool))
(= (succeedsPredicate $pattern)
   (quote
     (case
       (translatePredicate
         (catch (Predicate $pattern) $_ fail))
       (($item True) (Empty False)))))
!(add-translator-rule! succeedsPredicate)

!(test
    (succeedsPredicate (&self missing a b))
    False)

(friend a b)
(friend c d)

!(test
    (collapse
      (if (succeedsPredicate (&self friend $x $y))
          ($x $y)
          NotFound))
    ((a b) (c d)))
