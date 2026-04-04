!(include mork)

; join
!(bind! &join-lhs (mork:new-space))
!(bind! &join-rhs (mork:new-space))
!(mork:add-atom &join-lhs (edge a b))
!(mork:add-atom &join-lhs (edge c d))
!(mork:add-atom &join-rhs (edge c d))
!(mork:add-atom &join-rhs (edge e f))
!(bind! &joined (mork:join &join-lhs &join-rhs))
!(assertEqualToResult (get-type &joined) (MorkSpace))
!(assertEqualToResult (mork:size &joined) (3))
!(assertEqual
  (collapse (mork:get-atoms &joined))
  ((edge a b) (edge c d) (edge e f)))

; subtract
!(bind! &sub-lhs (mork:new-space))
!(bind! &sub-rhs (mork:new-space))
!(mork:add-atom &sub-lhs (edge c d))
!(mork:add-atom &sub-lhs (edge q r))
!(mork:add-atom &sub-rhs (edge c d))
!(mork:add-atom &sub-rhs (edge e f))
!(bind! &subtracted (mork:subtract &sub-lhs &sub-rhs))
!(assertEqualToResult (get-type &subtracted) (MorkSpace))
!(assertEqualToResult (mork:size &subtracted) (1))
!(assertEqual
  (collapse (mork:get-atoms &subtracted))
  ((edge q r)))
!(assertEqualToResult (mork:step! &subtracted 1) (0))

; valued-prefix restrict
!(bind! &prefix-lhs (mork:new-space))
!(bind! &prefix-selector (mork:new-space))
!(mork:add-atom &prefix-lhs (edge c d))
!(mork:add-atom &prefix-lhs (edge q r))
!(mork:add-atom &prefix-selector (edge c d))
!(bind! &restricted (mork:prefix-restrict &prefix-lhs &prefix-selector))
!(assertEqualToResult (get-type &restricted) (MorkSpace))
!(assertEqualToResult (mork:size &restricted) (1))
!(assertEqual
  (collapse (mork:get-atoms &restricted))
  ((edge c d)))
