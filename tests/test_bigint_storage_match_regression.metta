; Persistent spaces store and match bigint atoms without degrading them to symbols.

!(bind! &bigint-space (new-space))

!(add-atom &bigint-space (n 354224848179261915075))

!(assertEqualToResult
  (match &bigint-space (n $x) $x)
  (354224848179261915075))
