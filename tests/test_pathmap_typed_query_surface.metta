!(bind! &native (new-space))
!(bind! &pathmap (new-space pathmap))

!(add-atom &native (: ax-1 (-> $a (-> $b $a))))
!(add-atom &pathmap (: ax-1 (-> $a (-> $b $a))))

!(assertEqualToResult (count-atoms &native) (1))
!(assertEqualToResult (count-atoms &pathmap) (1))

!(assertAlphaEqualToResult
  (match &native (: $x $t) (: $x $t))
  ((: ax-1 (-> $a (-> $b $a)))))

!(assertAlphaEqualToResult
  (match &pathmap (: $x $t) (: $x $t))
  ((: ax-1 (-> $a (-> $b $a)))))
