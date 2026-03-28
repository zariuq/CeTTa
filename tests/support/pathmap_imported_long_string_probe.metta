!(bind! &probe (new-space))
!(add-atom &probe (row key "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail"))
!(assertEqualToResult
  (match &probe (row key $x) $x)
  ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail"))
