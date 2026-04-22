(= (mfail $x) (empty))
(= (sf $f1 $f2 $arg) (superpose (($f1 $arg) ($f2 $arg))))

!(assertAlphaEqual (mfail) (mfail))
!(assertAlphaEqual ((+ 1) 1) 2)
!(assertAlphaEqual (collapse (sf (+ 1) (mfail) 1)) (2))
