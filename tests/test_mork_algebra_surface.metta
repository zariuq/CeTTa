!(include mork)

; clone
!(bind! &clone-src (mork:new-space))
!(mork:add-atom &clone-src (edge a b))
!(mork:add-atom &clone-src (edge c d))
!(bind! &clone-copy (mork:clone &clone-src))
!(mork:add-atom &clone-copy (edge x y))
!(assertEqualToResult (mork:size &clone-src) (2))
!(assertEqualToResult (mork:match &clone-src (edge a b) ok) (ok))
!(assertEqualToResult (mork:match &clone-src (edge c d) ok) (ok))
!(assertEqualToResult (mork:size &clone-copy) (3))
!(assertEqualToResult (mork:match &clone-copy (edge x y) ok) (ok))

; join mutates destination and leaves rhs alone
!(bind! &join-lhs (mork:clone &clone-src))
!(bind! &join-rhs (mork:new-space))
!(mork:add-atom &join-rhs (edge b c))
!(mork:add-atom &join-rhs (edge e f))
!(assertEqual
  (mork:join &join-lhs &join-rhs)
  ())
!(assertEqualToResult (mork:size &join-lhs) (4))
!(assertEqualToResult (mork:match &join-lhs (edge e f) ok) (ok))
!(assertEqualToResult
  (mork:match &join-lhs (, (edge a $y) (edge $y c)) ok)
  (ok))
!(assertEqualToResult
  (mork:match &join-lhs (, (edge b $y) (edge $y d)) ok)
  (ok))
!(assertEqualToResult (mork:size &join-rhs) (2))
!(assertEqual (mork:join &join-lhs &join-lhs) ())
!(assertEqualToResult (mork:size &join-lhs) (4))

; meet mutates destination
!(bind! &meet-lhs (mork:new-space))
!(bind! &meet-rhs (mork:new-space))
!(mork:add-atom &meet-lhs (edge c d))
!(mork:add-atom &meet-lhs (edge q r))
!(mork:add-atom &meet-rhs (edge c d))
!(mork:add-atom &meet-rhs (edge e f))
!(assertEqual
  (mork:meet &meet-lhs &meet-rhs)
  ())
!(assertEqualToResult (mork:size &meet-lhs) (1))
!(assertEqualToResult (mork:match &meet-lhs (edge c d) ok) (ok))
!(assertEqual (mork:meet &meet-lhs &meet-lhs) ())
!(assertEqualToResult (mork:size &meet-lhs) (1))

; subtract mutates destination
!(bind! &sub-lhs (mork:new-space))
!(bind! &sub-rhs (mork:new-space))
!(mork:add-atom &sub-lhs (edge c d))
!(mork:add-atom &sub-lhs (edge q r))
!(mork:add-atom &sub-rhs (edge c d))
!(mork:add-atom &sub-rhs (edge e f))
!(assertEqual
  (mork:subtract &sub-lhs &sub-rhs)
  ())
!(assertEqualToResult (mork:size &sub-lhs) (1))
!(assertEqualToResult (mork:match &sub-lhs (edge q r) ok) (ok))
!(assertEqualToResult (mork:step! &sub-lhs 1) (0))
!(assertEqual (mork:subtract &sub-lhs &sub-lhs) ())
!(assertEqualToResult (mork:size &sub-lhs) (0))

; valued-prefix restrict mutates destination
!(bind! &prefix-lhs (mork:new-space))
!(bind! &prefix-selector (mork:new-space))
!(mork:add-atom &prefix-lhs (edge c d))
!(mork:add-atom &prefix-lhs (edge q r))
!(mork:add-atom &prefix-selector (edge c d))
!(assertEqual
  (mork:prefix-restrict &prefix-lhs &prefix-selector)
  ())
!(assertEqualToResult (mork:size &prefix-lhs) (1))
!(assertEqualToResult (mork:match &prefix-lhs (edge c d) ok) (ok))
!(assertEqual (mork:prefix-restrict &prefix-lhs &prefix-lhs) ())
!(assertEqualToResult (mork:size &prefix-lhs) (1))
