!(include mork)

(= (path-tag a c) strong)
(= (path-why a c) (two-hop a b c))

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))

!(assertEqualToResult
  (collapse (mork:match &ws (edge $x $y) ($x $y)))
  (((a b) (b c))))
!(assertEqualToResult
  (mork:step! &ws 4)
  (1))
!(assertEqualToResult
  (collapse (mork:match &ws (path $x $y) ($x $y)))
  (((a c))))

!(bind! &path-ac (mork:path-of-atom (path a c)))
!(bind! &selector (mork:new-space))
!(mork:add-atom &selector (path a c))
!(bind! &derived-only (mork:prefix-restrict &ws &selector))

!(assertEqualToResult
  (mork:size &derived-only)
  (1))
!(assertEqualToResult
  (collapse (mork:match &derived-only (path $x $y) ($x $y)))
  (((a c))))

!(bind! &z (mork:zipper &derived-only))
!(assertEqualToResult
  (mork:zipper-descend-until! &z)
  (True))
!(assertEqualToResult
  (mork:zipper-path-bytes &z)
  (&path-ac))
!(assertEqualToResult
  (mork:zipper-path-exists &z)
  (True))
!(assertEqualToResult
  (mork:zipper-is-val &z)
  (True))
!(assertEqualToResult
  (mork:zipper-close! &z)
  (()))

!(bind! &oz (mork:overlay-zipper &derived-only &selector))
!(assertEqualToResult
  (mork:overlay-zipper-descend-until! &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-path-bytes &oz)
  (&path-ac))
!(assertEqualToResult
  (mork:overlay-zipper-path-exists &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-is-val &oz)
  (True))
!(assertEqualToResult
  (mork:overlay-zipper-close! &oz)
  (()))

!(assertEqualToResult
  (collapse
    (mork:match &derived-only (path $x $y)
      (annotated $x $y (path-tag $x $y) (path-why $x $y))))
  (((annotated a c strong (two-hop a b c)))))
