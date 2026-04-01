!(bind! &q (new-space queue))
!(add-atom &q a)
!(add-atom &q b)
!(add-atom &q c)
!(space-pop &q)
!(assertEqualToResult
  (with-space-snapshot $snap &q (collapse (get-atoms $snap)))
  ((b c)))
!(assertEqualToResult
  (collapse (get-atoms &q))
  ((b c)))
