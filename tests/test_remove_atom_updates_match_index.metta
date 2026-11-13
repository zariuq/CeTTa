; Removing atoms must invalidate and rebuild the match backend coherently.

!(bind! &s (new-space))
!(add-atom &s (rel a))
!(add-atom &s (rel b))
!(assertEqualToResult (match &s (rel $x) $x) (a b))
!(remove-atom &s (rel a))
!(assertEqual (collapse (match &s (rel $x) $x)) (b))
!(add-atom &s (rel c))
!(assertEqualToResult (match &s (rel $x) $x) (b c))
