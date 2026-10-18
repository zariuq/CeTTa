; A nested match's legs before its last are read before any body runs, so
; over one PathMap space their conjunction is read through the space's
; index: one indexed query for each chain below.
!(bind! &data (new-space pathmap))
!(add-atoms &data ((edge a b) (edge a b) (edge b c) (edge c d) (edge c e) (edge d f)))
!(reset-runtime-stats!)

; Each of the two (edge a b) rows reaches every extension.
!(assertEqualToResult
  (match &data (edge $x $y)
    (match &data (edge $y $z)
      (match &data (edge $z $w)
        (path $x $y $z $w))))
  ((path a b c d) (path a b c e) (path a b c d) (path a b c e)
   (path b c d f)))

; Three legs before the last.
!(assertEqualToResult
  (match &data (edge $x $y)
    (match &data (edge $y $z)
      (match &data (edge $z $w)
        (match &data (edge $w $v)
          (walk $x $y $z $w $v)))))
  ((walk a b c d f) (walk a b c d f)))

!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-indexed-query $n) $n)
  (2))
