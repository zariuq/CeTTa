!(import! &self mork)

; Canonical live-space usage: ordinary space API over a MORK-backed space.
!(bind! &live (new-space mork))
!(add-atom &live (edge a b))
!(add-atom &live (edge b c))
!(assertEqual
  (match &live (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))
!(assertEqualToResult
  (size &live)
  (2))

; Canonical MORK-specific surface: ACT attach / dump / load.
!(assertEqual
  (mork:dump! &live "../runtime/test_mork_kiss_examples.act")
  ())
!(bind! &compiled (mork:open-act "../runtime/test_mork_kiss_examples.act"))
!(assertEqual
  (match &compiled (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))

!(bind! &dst (new-space mork))
!(assertEqual
  (mork:load-act! &dst "../runtime/test_mork_kiss_examples.act")
  ())
!(assertEqual
  (match &dst (edge $x $y) ($x $y))
  (superpose ((a b) (b c))))
!(assertEqualToResult
  (size &dst)
  (2))
