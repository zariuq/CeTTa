!(import! &self mork)
!(import! &self list)

!(bind! &append-native (new-space native))
!(assertEqualToResult
  (size
    (collapse
      (let $i (superpose (0 1 2 3))
        (add-atom &append-native (friend sam $i)))))
  (4))
!(assertEqualToResult (size &append-native) (4))
!(assertEqualToResult
  (size (collapse (match &append-native (friend sam $x) $x)))
  (4))

!(bind! &append-pathmap (new-space pathmap))
!(let $ignored
  (collapse
    (let $xs (eval (list:range 5))
      (let $i (superpose $xs)
        (add-atom &append-pathmap (friend alex $i)))))
  ())
!(assertEqualToResult (size &append-pathmap) (5))
!(assertEqualToResult
  (size (collapse (match &append-pathmap (friend alex $x) $x)))
  (5))

!(bind! &append-nodup (new-space native))
!(let $ignored
  (collapse
    (let $i (superpose (1 1 2 2 3))
      (add-atom-nodup &append-nodup (seen $i))))
  ())
!(assertEqualToResult (size &append-nodup) (3))

!(bind! &append-mork (mork:new-space))
!(assertEqualToResult
  (size
    (collapse
      (let $i (superpose (0 1 2 3))
        (mork:add-atom &append-mork (edge sam $i)))))
  (4))
!(assertEqualToResult (mork:size &append-mork) (4))
!(assertEqualToResult
  (size (collapse (mork:match &append-mork (edge sam $x) $x)))
  (4))

!(bind! &append-error-native (new-space native))
!(assertEqualToResult
  (collapse
    (let $i (superpose (1 (Error append-probe fail)))
      (add-atom &append-error-native (errcase $i))))
  ((())))
!(assertEqualToResult (size &append-error-native) (1))

!(bind! &append-error-let (new-space native))
!(assertEqualToResult
  (let $i (superpose (1 (Error let-probe fail)))
    (add-atom &append-error-let (errlet $i)))
  (()))
!(assertEqualToResult (size &append-error-let) (1))

!(bind! &append-error-all (new-space native))
!(assertEqualToResult
  (collapse
    (let $i (superpose ((Error append-only fail)))
      (add-atom &append-error-all (errall $i))))
  (((Error append-only fail))))
!(assertEqualToResult (size &append-error-all) (0))
