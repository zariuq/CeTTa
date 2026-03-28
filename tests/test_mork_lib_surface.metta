!(import! &self mork)
!(bind! &h (new-space hash))
!(bind! &mh (mork-space-new hash))
!(assertEqualToResult
  (let (mork-space (discipline $kind) (engine $engine) (match-backend $backend) (capabilities $caps))
    (mork-space-spec &h)
    $kind)
  (hash))
!(assertEqualToResult
  (let (mork-space (discipline hash) (engine $engine) (match-backend $backend) (capabilities $caps))
    (mork-space-spec &h)
    $caps)
  ((unordered hash exact match add remove len snapshot nodup)))
!(assertEqualToResult
  (let (mork-space (discipline hash) (engine $engine) (match-backend $backend) (capabilities $caps))
    (mork-space-spec &h)
    (== $engine (mork-space-engine &h)))
  (True))
!(assertEqualToResult
  (let (mork-space (discipline hash) (engine $engine) (match-backend $backend) (capabilities $caps))
    (mork-space-spec &h)
    (== $backend (mork-space-backend &h)))
  (True))
!(assertEqualToResult
  (== (mork-backed-space? &h) (== (mork-space-engine &h) mork))
  (True))
!(assertEqualToResult
  (mork-backed-space? &mh)
  (True))
!(assertEqualToResult
  (mork-space-engine &mh)
  (mork))
!(assertEqualToResult
  (mork-space-backend &mh)
  (pathmap-imported))
!(assertEqualToResult
  (mork-space-spec &mh)
  ((mork-space
      (discipline hash)
      (engine mork)
      (match-backend pathmap-imported)
      (capabilities (unordered hash exact match add remove len snapshot nodup)))))
!(assertEqualToResult
  (mork-space-count &mh)
  (0))
!(mork-space-add! &mh (edge a b))
!(mork-space-add! &mh (edge b c))
!(assertEqualToResult
  (mork-space-count &mh)
  (2))
!(assertEqualToResult
  (mork-space-query &mh (edge $x $y) ($x $y))
  ((a b) (b c)))
!(assertEqual
  (mork-space-atoms &mh)
  ((edge a b) (edge b c)))
!(bind! &snap (mork-space-snapshot &mh))
!(mork-space-add! &mh (edge c d))
!(assertEqualToResult
  (mork-space-query &snap (edge $x $y) ($x $y))
  ((a b) (b c)))
!(assertEqualToResult
  (mork-space-query &mh (edge $x $y) ($x $y))
  ((a b) (b c) (c d)))
!(bind! &built (mork-space-from-atoms hash ((edge x y) (edge y z))))
!(assertEqualToResult
  (mork-space-query &built (edge $x $y) ($x $y))
  ((x y) (y z)))
