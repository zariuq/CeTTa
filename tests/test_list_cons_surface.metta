!(import! &self list_cons)

!(assertEqual (conslist:nil? (conslist:nil)) True)
!(assertEqual (conslist:cons? (conslist:cons a (conslist:nil))) True)
!(assertEqual (conslist:head (conslist:cons a (conslist:nil))) a)
!(assertEqual (conslist:tail (conslist:cons a (conslist:nil))) (conslist:nil))
!(assertEqual (conslist:to-expr (conslist:from-expr (a b c))) (a b c))
!(assertEqual (conslist:length (conslist:from-expr (a b c))) 3)
!(assertEqual
  (conslist:append (conslist:from-expr (a b)) (conslist:from-expr (c d)))
  (conslist:from-expr (a b c d)))
!(assertEqual
  (conslist:reverse (conslist:from-expr (a b c)))
  (conslist:from-expr (c b a)))
!(assertEqual
  (conslist:map (conslist:from-expr (1 2 3)) $x (+ $x 1))
  (conslist:from-expr (2 3 4)))
!(assertEqual
  (conslist:filter (conslist:from-expr (1 2 3 4)) $x (> $x 2))
  (conslist:from-expr (3 4)))
!(assertEqual
  (conslist:foldl (conslist:from-expr (1 2 3 4)) 0 $acc $item (+ $acc $item))
  10)
