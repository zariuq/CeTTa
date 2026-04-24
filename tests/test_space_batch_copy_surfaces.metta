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

!(bind! &native-collapse-copy (new-space native))
!(add-atoms &native-collapse-copy (collapse (get-atoms &native-batch)))
!(assertEqualToResult (size &native-collapse-copy) (3))
!(assertEqualToResult
  (size (collapse (match &native-collapse-copy (friend sam $x) $x)))
  (2))

!(bind! &native-direct-copy (new-space native))
!(add-atoms &native-direct-copy (get-atoms &native-batch))
!(assertEqualToResult (size &native-direct-copy) (3))
!(assertEqualToResult
  (size (collapse (match &native-direct-copy (friend sam $x) $x)))
  (2))

!(bind! &pathmap-collapse-copy (new-space pathmap))
!(add-atoms &pathmap-collapse-copy (collapse (get-atoms &pathmap-batch)))
!(assertEqualToResult (size &pathmap-collapse-copy) (3))
!(assertEqualToResult
  (size (collapse (match &pathmap-collapse-copy (friend sam $x) $x)))
  (2))

!(bind! &pathmap-direct-copy (new-space pathmap))
!(add-atoms &pathmap-direct-copy (get-atoms &pathmap-batch))
!(assertEqualToResult (size &pathmap-direct-copy) (3))
!(assertEqualToResult
  (size (collapse (match &pathmap-direct-copy (friend sam $x) $x)))
  (2))

!(bind! &native-let-copy (new-space pathmap))
!(let $atoms (collapse (get-atoms &native-batch))
   (add-atoms &native-let-copy $atoms))
!(assertEqualToResult (size &native-let-copy) (3))
!(assertEqualToResult
  (size (collapse (match &native-let-copy (friend sam $x) $x)))
  (2))

!(bind! &pathmap-let-copy (new-space pathmap))
!(let $atoms (collapse (get-atoms &pathmap-batch))
   (add-atoms &pathmap-let-copy $atoms))
!(assertEqualToResult (size &pathmap-let-copy) (3))
!(assertEqualToResult
  (size (collapse (match &pathmap-let-copy (friend sam $x) $x)))
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

!(bind! &pathmap-mork-direct-copy (new-space pathmap))
!(add-atoms &pathmap-mork-direct-copy (mork:get-atoms &mork-source))
!(assertEqualToResult (size &pathmap-mork-direct-copy) (3))
!(assertEqualToResult
  (size (collapse (match &pathmap-mork-direct-copy (friend sam $x) $x)))
  (2))

!(bind! &mork-collapse-copy (mork:new-space))
!(mork:add-atoms &mork-collapse-copy (collapse (mork:get-atoms &mork-source)))
!(assertEqualToResult (mork:size &mork-collapse-copy) (3))
!(assertEqualToResult
  (size (collapse (mork:match &mork-collapse-copy (friend sam $x) $x)))
  (2))

!(bind! &mork-direct-copy (mork:new-space))
!(mork:add-atoms &mork-direct-copy (mork:get-atoms &mork-source))
!(assertEqualToResult (mork:size &mork-direct-copy) (3))
!(assertEqualToResult
  (size (collapse (mork:match &mork-direct-copy (friend sam $x) $x)))
  (2))

!(bind! &mork-let-copy (mork:new-space))
!(let $atoms (collapse (mork:get-atoms &mork-source))
   (mork:add-atoms &mork-let-copy $atoms))
!(assertEqualToResult (mork:size &mork-let-copy) (3))
!(assertEqualToResult
  (size (collapse (mork:match &mork-let-copy (friend sam $x) $x)))
  (2))

!(bind! &mork-from-pathmap-direct (mork:new-space))
!(mork:add-atoms &mork-from-pathmap-direct (get-atoms &pathmap-batch))
!(assertEqualToResult (mork:size &mork-from-pathmap-direct) (3))
!(assertEqualToResult
  (size (collapse (mork:match &mork-from-pathmap-direct (friend sam $x) $x)))
  (2))

!(pragma! mork-space-sugar allow)
!(bind! &mork-generic-from-pathmap (mork:new-space))
!(reset-runtime-stats!)
!(add-atoms &mork-generic-from-pathmap (get-atoms &pathmap-batch))
!(bind! &generic-mork-stats (runtime-stats!))
!(assertEqualToResult (mork:size &mork-generic-from-pathmap) (3))
!(assertEqualToResult
  (match &generic-mork-stats (runtime-counter mork-add-call $n) $n)
  (0))
!(assertEqualToResult
  (match &generic-mork-stats (runtime-counter mork-add-batch-call $n) $n)
  (0))
!(assertEqualToResult
  (size (collapse (mork:match &mork-generic-from-pathmap (friend sam $x) $x)))
  (2))

!(bind! &pathmap-counted-dup (new-space pathmap))
!(add-atom &pathmap-counted-dup (dup counted))
!(add-atom &pathmap-counted-dup (dup counted))
!(assertEqualToResult (size &pathmap-counted-dup) (2))
!(bind! &mork-structural-from-counted (mork:new-space))
!(add-atoms &mork-structural-from-counted (get-atoms &pathmap-counted-dup))
!(assertEqualToResult (mork:size &mork-structural-from-counted) (1))

!(bind! &mork-structural-dup (mork:new-space))
!(mork:add-atom &mork-structural-dup (dup structural))
!(mork:add-atom &mork-structural-dup (dup structural))
!(assertEqualToResult (mork:size &mork-structural-dup) (1))
!(bind! &pathmap-counted-from-structural (new-space pathmap))
!(add-atoms &pathmap-counted-from-structural (mork:get-atoms &mork-structural-dup))
!(assertEqualToResult (size &pathmap-counted-from-structural) (1))
