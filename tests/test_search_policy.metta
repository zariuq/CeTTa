; search-policy: explicit native search-lane selection plus meaningful
; result-order control for stream consumers.

(= (coin) (superpose (heads tails)))
(= (words) (superpose (b aa ab)))

!(assertEqual
  (search-policy recursive-dependent-proof)
  (search-policy recursive-dependent-proof))
!(assertEqualToResult
  (select (search-policy recursive-dependent-proof) (coin))
  (heads))
!(assertEqual
  (select 2 (search-policy recursive-dependent-proof) (coin))
  (heads tails))
!(assertEqual
  (collect (search-policy recursive-dependent-proof) (coin))
  (heads tails))
!(assertEqualToResult
  (select (search-policy recursive-dependent-proof (order reverse)) (coin))
  (tails))
!(assertEqual
  (collect (search-policy recursive-dependent-proof (order reverse)) (coin))
  (tails heads))
!(assertEqualToResult
  (select (search-policy recursive-dependent-proof (order lex)) (words))
  (aa))
!(assertEqual
  (collect (search-policy recursive-dependent-proof (order lex)) (words))
  (aa ab b))
!(assertEqualToResult
  (select (search-policy recursive-dependent-proof (order shortlex)) (words))
  (b))
!(assertEqual
  (collect (search-policy recursive-dependent-proof (order shortlex)) (words))
  (b aa ab))
