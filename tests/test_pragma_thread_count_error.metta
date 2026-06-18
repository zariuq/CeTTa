; HE rejects invalid thread-count pragmas instead of silently clamping them.
!(assertEqualToResult
  (pragma! num-threads -1)
  ((Error (pragma! num-threads -1) UnsignedIntegerIsExpected)))
!(assertEqualToResult
  (pragma! num-threads 2048)
  ((Error (pragma! num-threads 2048) ThreadCountOutOfRange)))
