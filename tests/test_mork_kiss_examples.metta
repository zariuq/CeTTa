!(import! &self mork)

; Canonical live-space usage: explicit MorkSpace API.
!(bind! &live (mork:new-space))
!(mork:add-atom &live (edge a b))
!(mork:add-atom &live (edge b c))
!(assertEqual
  (mork:match &live (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))
!(assertEqualToResult
  (mork:size &live)
  (2))

; Canonical MORK-specific surface: ACT attach / dump / load.
!(assertEqual
  (mork:dump! &live "../runtime/test_mork_kiss_examples.act")
  ())
!(bind! &compiled (mork:open-act "../runtime/test_mork_kiss_examples.act"))
!(assertEqual
  (mork:match &compiled (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))

!(bind! &dst (mork:new-space))
!(assertEqual
  (mork:load-act! &dst "../runtime/test_mork_kiss_examples.act")
  ())
!(assertEqual
  (mork:match &dst (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))
!(assertEqualToResult
  (mork:size &dst)
  (2))
