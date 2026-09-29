; lib/list over list values and over expressions: each operation returns the
; kind it was given, and one over two sequences the kind of the first.
!(import! &self list)

; Length, index, head, rest, emptiness.
!(assertEqual (list:len [a b c]) 3)
!(assertEqual (list:at [a b c] 2) c)
!(assertEqual (list:first [a b]) a)
!(assertEqual (list:rest [a b]) [b])
!(assertEqual (list:rest (a b)) (b))
!(assertEqual (list:is-empty []) True)
!(assertEqual (list:is-empty ()) True)
!(assertEqual (list:is-empty [a]) False)

; Construction and append keep the kind.
!(assertEqual (list:cons x [a]) [x a])
!(assertEqual (list:cons x (a)) (x a))
!(assertEqual (list:push [a] b) [a b])
!(assertEqual (list:append [a] [b c]) [a b c])
!(assertEqual (list:append (a) (b c)) (a b c))

; Map, filter and fold read the elements; map and filter keep the kind.
!(assertEqual (list:map [1 2] $x (+ $x 1)) [2 3])
!(assertEqual (list:map (1 2) $x (+ $x 1)) (2 3))
!(assertEqual (list:filter [1 2 3] $x (> $x 1)) [2 3])
!(assertEqual (list:filter (1 2 3) $x (> $x 1)) (2 3))
!(assertEqual (list:foldl [1 2 3] 0 $acc $x (+ $acc $x)) 6)
!(assertEqual (list:foldl (1 2 3) 0 $acc $x (+ $acc $x)) 6)

; Reverse, sorting, uniqueness, removal, prefixes.
!(assertEqual (list:reverse [a b c]) [c b a])
!(assertEqual (list:reverse (a b c)) (c b a))
!(assertEqual (list:reverse []) [])
!(assertEqual (list:sort [c a b]) [a b c])
!(assertEqual (list:sort-by list:number-ascending [3 1 2]) [1 2 3])
!(assertEqual (list:sort-by list:number-ascending (3 1 2)) (1 2 3))
!(assertEqual (list:unique [a b a]) [a b])
!(assertEqual (list:unique-by list:atom-equal [a b a]) [a b])
!(assertEqual (list:remove-all a [a b a]) [b])
!(assertEqual (list:remove-all-by list:atom-equal a [a b a]) [b])
!(assertEqual (list:take 2 [a b c]) [a b])
!(assertEqual (list:take 2 (a b c)) (a b))
!(assertEqual (list:split-half [a b c d]) ([a b] [c d]))
!(assertEqual (list:merge-by list:number-ascending [1 3] [2 4]) [1 2 3 4])

; From and to expression; a sequence of the asked kind is returned as it is.
!(assertEqual (list:from-expression (a b)) [a b])
!(assertEqual (list:to-expression [a b]) (a b))
!(assertEqual (list:from-expression [a b]) [a b])
!(assertEqual (list:to-expression (a b)) (a b))
!(assertEqual (== (list:from-expression ()) ()) False)
