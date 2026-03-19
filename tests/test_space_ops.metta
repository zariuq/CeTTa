; Space operations — new-space, bind!, add-atom, remove-atom
!(bind! &ws (new-space))
!(add-atom &ws fact1)
!(add-atom &ws fact2)
!(assertEqualToResult (match &ws $x $x) (fact1 fact2))
!(remove-atom &ws fact1)
!(assertEqualToResult (match &ws $x $x) (fact2))
