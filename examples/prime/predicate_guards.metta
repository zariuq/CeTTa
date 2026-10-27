; Ordinary runtime validation of finite lists of integer literals.
; This guard does not publish a theorem about the modulo implementation.
!(import! &self list)

(= (all-even? $xs)
   (foldl-atom $xs True $ok $x (and $ok (== (% $x 2) 0))))
(= (reverse-even $xs)
   (if (all-even? $xs) (list:reverse $xs) (superpose ())))

!(all-even? ())
!(all-even? (2 4))
!(all-even? (2 3))
!(all-even? (-2 0 4))
!(reverse-even (2 4))
; Failure supplies no result; collapse makes that observable as ().
!(collapse (reverse-even (2 3)))
!(reverse-even ())
