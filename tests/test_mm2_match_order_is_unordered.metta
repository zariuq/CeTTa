; The answers of a PathMap match are a bag: every rule's output is there,
; and no order of them, insertion order or any other, is promised or tested.

!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (go))

; Rules fire in priority order: 0, 1, 2, 3
!(mork:add-atom &ws (exec (0 a) (, (go)) (O (+ (out zebra)))))
!(mork:add-atom &ws (exec (1 b) (, (go)) (O (+ (out aardvark)))))
!(mork:add-atom &ws (exec (2 c) (, (go)) (O (+ (out mango)))))
!(mork:add-atom &ws (exec (3 d) (, (go)) (O (+ (out cherry)))))

; All 4 fire
!(assertEqualToResult (mork:step! &ws 4) (4))

; All 4 are present, compared as a bag.
!(assertEqual
  (mork:match &ws (out $x) $x)
  (superpose (zebra aardvark mango cherry)))
