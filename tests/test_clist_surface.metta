!(import! &self clist)

!(assertEqual (clist:empty? (clist:nil)) True)
!(assertEqual (clist:cons? (clist:cons a (clist:nil))) True)
!(assertEqual (clist:first (clist:cons a (clist:nil))) a)
!(assertEqual (clist:rest (clist:cons a (clist:nil))) (clist:nil))
!(assertEqual (clist:to-list (clist:from-list (a b c))) (a b c))
!(assertEqual (clist:len (clist:from-list (a b c))) 3)
!(assertEqual
  (clist:append (clist:from-list (a b)) (clist:from-list (c d)))
  (clist:from-list (a b c d)))
!(assertEqual
  (clist:reverse (clist:from-list (a b c)))
  (clist:from-list (c b a)))
!(assertEqual
  (clist:map (clist:from-list (1 2 3)) $x (+ $x 1))
  (clist:from-list (2 3 4)))
!(assertEqual
  (clist:filter (clist:from-list (1 2 3 4)) $x (> $x 2))
  (clist:from-list (3 4)))
!(assertEqual
  (clist:foldl (clist:from-list (1 2 3 4)) 0 $acc $item (+ $acc $item))
  10)
!(assertEqual (clist:range 5) (clist:from-list (0 1 2 3 4)))
!(assertEqual (clist:range 2 6) (clist:from-list (2 3 4 5)))
!(assertEqual (clist:range 4 1) (clist:nil))
!(assertEqual (clist:repeat 3 z) (clist:from-list (z z z)))
!(assertEqual (clist:repeat 0 z) (clist:nil))
