; Positive test: PathMap match order is NOT insertion order.
; We prove this by showing the multiset is correct while the
; ordered assertion would fail.

!(bind! &ws (new-space mork))
!(add-atom &ws (go))

; Rules fire in priority order: 0, 1, 2, 3
!(add-atom &ws (exec (0 a) (, (go)) (O (+ (out zebra)))))
!(add-atom &ws (exec (1 b) (, (go)) (O (+ (out aardvark)))))
!(add-atom &ws (exec (2 c) (, (go)) (O (+ (out mango)))))
!(add-atom &ws (exec (3 d) (, (go)) (O (+ (out cherry)))))

; All 4 fire
!(assertEqualToResult (step! &ws 4) (4))

; Correct: multiset comparison — all 4 present regardless of order
!(assertEqual
  (match &ws (out $x) $x)
  (superpose (zebra aardvark mango cherry)))

; Prove order differs: collect the actual sequence and show it's
; NOT (zebra aardvark mango cherry) — i.e., NOT insertion order
!(assertEqualToResult
  (if (== (collapse (match &ws (out $x) $x))
          (zebra aardvark mango cherry))
      order-matches-insertion
      order-differs-from-insertion)
  (order-differs-from-insertion))
