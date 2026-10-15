; add-atom-nodup keeps one row of a float term, read by its bits: a stored
; float is found again by exact membership, so a second insertion adds
; nothing and a removal finds the row.  0.0 and -0.0 are two terms.

!(bind! &floats (new-space))
!(add-atom-nodup &floats (row 1.5))
!(add-atom-nodup &floats (row 1.5))
!(add-atom-nodup &floats (row -2.25e10))
!(add-atom-nodup &floats (row -2.25e10))
!(assertEqualToResult (space-len &floats) (2))

!(bind! &zeros (new-space))
!(add-atom-nodup &zeros (zero 0.0))
!(add-atom-nodup &zeros (zero -0.0))
!(add-atom-nodup &zeros (zero -0.0))
!(assertEqualToResult (space-len &zeros) (2))
!(remove-atom &zeros (zero -0.0))
!(assertEqualToResult (space-len &zeros) (1))
!(remove-atom &floats (row 1.5))
!(assertEqualToResult (space-len &floats) (1))
