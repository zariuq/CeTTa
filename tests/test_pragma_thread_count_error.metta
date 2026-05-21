; HE rejects invalid thread-count pragmas instead of silently clamping them.
!(assertEqualToResult
  (pragma! hyperpose-threads -1)
  ((Error (pragma! hyperpose-threads -1) UnsignedIntegerIsExpected)))
!(assertEqualToResult
  (pragma! hyperpose-threads 2048)
  ((Error (pragma! hyperpose-threads 2048) ThreadCountOutOfRange)))
!(assertEqualToResult
  (pragma! rho-step-threads threaded)
  ((Error (pragma! rho-step-threads threaded) UnsignedIntegerIsExpected)))
