!(import! &self mork)

!(bind! &native-batch (new-space native))
!(add-atoms &native-batch ((friend sam 1) (friend sam 2) (friend alex 3)))
!(assertEqualToResult (size &native-batch) (3))
!(assertEqualToResult
  (size (collapse (match &native-batch (friend sam $x) $x)))
  (2))

!(bind! &pathmap-batch (new-space pathmap))
!(add-atoms &pathmap-batch ((friend sam 1) (friend sam 2) (friend alex 3)))
!(assertEqualToResult (size &pathmap-batch) (3))
!(assertEqualToResult
  (size (collapse (match &pathmap-batch (friend sam $x) $x)))
  (2))

!(bind! &mork-source (mork:new-space))
!(mork:add-atoms &mork-source ((friend sam 1) (friend sam 2) (friend alex 3)))
!(assertEqualToResult (mork:size &mork-source) (3))

!(bind! &native-copy (new-space native))
!(let $atoms (collapse (mork:get-atoms &mork-source))
   (add-atoms &native-copy $atoms))
!(assertEqualToResult (size &native-copy) (3))
!(assertEqualToResult
  (size (collapse (match &native-copy (friend sam $x) $x)))
  (2))

!(bind! &pathmap-copy (new-space pathmap))
!(let $atoms (collapse (mork:get-atoms &mork-source))
   (add-atoms &pathmap-copy $atoms))
!(assertEqualToResult (size &pathmap-copy) (3))
!(assertEqualToResult
  (size (collapse (match &pathmap-copy (friend sam $x) $x)))
  (2))
