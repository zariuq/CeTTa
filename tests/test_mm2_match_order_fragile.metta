; Demonstrates that assertEqualToResult on match over a MORK space
; is fragile: PathMap's hash-trie order ≠ insertion order.

!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (go))

!(mork:add-atom &ws (exec (0 a) (, (go)) (O (+ (out zebra)))))
!(mork:add-atom &ws (exec (1 b) (, (go)) (O (+ (out aardvark)))))
!(mork:add-atom &ws (exec (2 c) (, (go)) (O (+ (out mango)))))
!(mork:add-atom &ws (exec (3 d) (, (go)) (O (+ (out cherry)))))

!(mork:step! &ws 4)

; Asserts insertion order — breaks if PathMap returns hash/trie order
!(assertEqualToResult
  (mork:match &ws (out $x) $x)
  (zebra aardvark mango cherry))
