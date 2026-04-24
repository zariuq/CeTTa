!(import! &self mork)

!(bind! &guard-src (new-space pathmap))
!(add-atom &guard-src (edge a b))
!(assertEqualToResult (size &guard-src) (1))

(= (guard-add-atoms $space $atoms)
  (guarded-add-seen $atoms))

!(bind! &guard-dst (new-space pathmap))
!(assertEqualToResult
  (let $atoms (collapse (get-atoms &guard-src))
    (guard-add-atoms &guard-dst $atoms))
  ((guarded-add-seen ((edge a b)))))
!(assertEqualToResult (size &guard-dst) (0))

(= (guard-get-atoms $space)
  (superpose ((edge c d))))

!(bind! &guard-dst2 (new-space pathmap))
!(let $atoms (collapse (guard-get-atoms &guard-src))
   (add-atoms &guard-dst2 $atoms))
!(assertEqualToResult (size &guard-dst2) (1))
!(assertEqualToResult
  (size (collapse (match &guard-dst2 (edge c d) (edge c d))))
  (1))

!(bind! &guard-mork-src (mork:new-space))
!(mork:add-atom &guard-mork-src (edge m n))
!(assertEqualToResult (mork:size &guard-mork-src) (1))

(= (guard-mork-add-atoms $space $atoms)
  (guarded-mork-add-seen $atoms))

!(bind! &guard-mork-dst (mork:new-space))
!(assertEqualToResult
  (let $atoms (collapse (mork:get-atoms &guard-mork-src))
    (guard-mork-add-atoms &guard-mork-dst $atoms))
  ((guarded-mork-add-seen ((edge m n)))))
!(assertEqualToResult (mork:size &guard-mork-dst) (0))

!(bind! &same-head-src (new-space pathmap))
!(add-atom &same-head-src (edge override visible))
!(bind! &same-head-dst (new-space pathmap))

(= (add-atoms $space $atoms)
  (same-head-add-atoms-visible $atoms))

!(assertIncludes
  (let $atoms (collapse (get-atoms &same-head-src))
    (add-atoms &same-head-dst $atoms))
  ((same-head-add-atoms-visible ((edge override visible))) ()))
!(assertEqualToResult (size &same-head-dst) (1))

!(bind! &same-head-direct-collapse-dst (new-space pathmap))
!(assertIncludes
  (add-atoms &same-head-direct-collapse-dst
    (collapse (get-atoms &same-head-src)))
  ((same-head-add-atoms-visible (collapse (get-atoms &same-head-src))) ()))
!(assertEqualToResult (size &same-head-direct-collapse-dst) (2))
!(assertEqualToResult
  (size (collapse
    (match &same-head-direct-collapse-dst
      (edge override visible)
      (edge override visible))))
  (0))

!(bind! &same-head-direct-stream-dst (new-space pathmap))
!(assertIncludes
  (add-atoms &same-head-direct-stream-dst (get-atoms &same-head-src))
  ((same-head-add-atoms-visible (get-atoms &same-head-src)) ()))
!(assertEqualToResult (size &same-head-direct-stream-dst) (2))
!(assertEqualToResult
  (size (collapse
    (match &same-head-direct-stream-dst
      (edge override visible)
      (edge override visible))))
  (0))
