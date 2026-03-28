!(import! &self mork)
!(bind! &h (new-space hash))
!(assertEqualToResult
  (let (mork-space (discipline $kind) (match-backend $backend) (capabilities $caps))
    (mork-space-spec &h)
    $kind)
  (hash))
!(assertEqualToResult
  (let (mork-space (discipline hash) (match-backend $backend) (capabilities $caps))
    (mork-space-spec &h)
    $caps)
  ((unordered hash exact match add remove len snapshot nodup)))
!(assertEqualToResult
  (let (mork-space (discipline hash) (match-backend $backend) (capabilities $caps))
    (mork-space-spec &h)
    (== $backend (mork-space-backend &h)))
  (True))
!(assertEqualToResult
  (== (mork-backed-space? &h) (== (mork-space-backend &h) pathmap-imported))
  (True))
