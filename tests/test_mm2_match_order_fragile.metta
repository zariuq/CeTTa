; Probe: match over a MORK space has backend-specific result order.
; The positive contract lives in test_mm2_match_order_is_unordered.metta.

!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (go))

!(mork:add-atom &ws (exec (0 a) (, (go)) (O (+ (out zebra)))))
!(mork:add-atom &ws (exec (1 b) (, (go)) (O (+ (out aardvark)))))
!(mork:add-atom &ws (exec (2 c) (, (go)) (O (+ (out mango)))))
!(mork:add-atom &ws (exec (3 d) (, (go)) (O (+ (out cherry)))))

!(mork:step! &ws 4)

!(println! (mork-match-observed-order
  (collapse (mork:match &ws (out $x) $x))))

!(println! (mork-match-insertion-order
  (zebra aardvark mango cherry)))
