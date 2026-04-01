; Demonstrates that assertEqualToResult on match over a MORK space
; is fragile: PathMap's hash-trie order ≠ insertion order.

!(bind! &ws (new-space mork))
!(add-atom &ws (go))

!(add-atom &ws (exec (0 a) (, (go)) (O (+ (out zebra)))))
!(add-atom &ws (exec (1 b) (, (go)) (O (+ (out aardvark)))))
!(add-atom &ws (exec (2 c) (, (go)) (O (+ (out mango)))))
!(add-atom &ws (exec (3 d) (, (go)) (O (+ (out cherry)))))

!(step! &ws 4)

; Asserts insertion order — breaks if PathMap returns hash/trie order
!(assertEqualToResult
  (match &ws (out $x) $x)
  (zebra aardvark mango cherry))
