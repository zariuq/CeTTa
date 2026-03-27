; HE-backed positive surface: `sealed` is a pure alpha-freshening helper.
; It must preserve syntax, freshen non-ignored variables consistently, and
; support the locally scoped-variable pattern used by HE stdlib helpers.

!(assertEqualToResult (sealed () (+ 1 2)) ((+ 1 2)))
!(assertEqualToResult (sealed () (quote (+ 1 2))) ((quote (+ 1 2))))

!(assertAlphaEqualToResult
  (sealed ($c) (quote (= ($a $x $c) ($b))))
  ((quote (= ($a $x $c) ($b)))))

!(assertAlphaEqualToResult
  (sealed $x (quote (= ($y $x))))
  ((quote (= ($y $x)))))

!(assertEqualToResult
  (let (quote ($sv $st)) (sealed () (quote ($x (output $x))))
    (let $sv (input $x) $st))
  ((output (input $x))))

!(pragma! type-check auto)
!(assertAlphaEqualToResult
  (sealed $x (quote (= ($y $x))))
  ((quote (= ($y $x)))))
