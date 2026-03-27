!(bind! &bag (new-space))
!(add-atom &bag (foo a))
!(add-atom &bag (foo b))
!(assertEqualToResult (get-atoms &bag) ((foo a) (foo b)))
