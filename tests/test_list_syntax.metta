!(import! &self list)

!(assertEqual (list:is-empty ()) True)
!(assertEqual (list:is-empty (a b)) False)
!(assertEqual (list:first (a b c)) a)
!(assertEqual (list:rest (a b c)) (b c))
!(assertEqual (list:len (a b c)) 3)
!(assertEqual (list:at (a b c) 1) b)
!(assertEqual (list:cons z (a b)) (z a b))
!(assertEqual (list:push (a b) c) (a b c))
!(assertEqual (list:append (a b) (c d)) (a b c d))
!(assertEqual (list:reverse (a b c)) (c b a))
!(assertEqual (list:remove-all a (b a a c a)) (b c))
!(assertEqual (list:remove-all z (b a c)) (b a c))
!(assertEqual (list:remove-all a ()) ())
!(assertEqual
  (list:remove-all (tag x) ((tag x) (tag y) (tag x)))
  ((tag y)))
; Outside the declared Expression domain, the grounded operation remains
; uninterpreted rather than inventing a cross-profile failure.
!(assertEqual
  (list:remove-all a not-an-expression)
  (remove-all-atom a not-an-expression))
!(assertEqual (list:unique (b a b c a)) (b a c))
!(assertEqual
  (list:unique ((tag x) (tag y) (tag x) (tag z)))
  ((tag x) (tag y) (tag z)))
!(assertEqual (list:map (1 2 3) $x (+ $x 1)) (2 3 4))
!(assertEqual (list:filter (1 2 3 4) $x (> $x 2)) (3 4))
!(assertEqual (list:foldl (1 2 3 4) 0 $acc $item (+ $acc $item)) 10)
!(assertEqual (list:range 5) (0 1 2 3 4))
!(assertEqual (list:range 2 6) (2 3 4 5))
!(assertEqual (list:range 4 1) ())
!(assertEqual (list:repeat 3 z) (z z z))
!(assertEqual (list:repeat 0 z) ())
!(assertEqual
  (list:sort ((aa) aa (z) b () "x"))
  (b () aa "x" (z) (aa)))
!(assertEqual (list:sort-numbers ()) ())
!(assertEqual (list:sort-numbers (7)) (7))
!(assertEqual (list:sort-numbers (5 -1 3 3 2 -4 5)) (-4 -1 2 3 3 5 5))
!(assertEqual
  (list:sort-by list:number-descending (5 -1 3 3 2 -4 5))
  (5 5 3 3 2 -1 -4))
!(assertEqual
  (list:len (list:sort-numbers (4 1 4 2 4 3)))
  (list:len (4 1 4 2 4 3)))
!(assertEqual
  (list:sort-numbers (list:sort-numbers (4 1 4 2 4 3)))
  (list:sort-numbers (4 1 4 2 4 3)))

(= (test:item-key-ascending (item $left-key $left-value)
                            (item $right-key $right-value))
   (<= $left-key $right-key))

(= (test:item-key-equal (item $left-key $left-value)
                         (item $right-key $right-value))
   (== $left-key $right-key))

(= (test:item-key (item $key $value))
   $key)

!(assertEqual
  (list:sort-by test:item-key-ascending
    ((item 1 first) (item 0 zero) (item 1 second) (item 1 third)))
  ((item 0 zero) (item 1 first) (item 1 second) (item 1 third)))
!(assertEqual
  (list:remove-all-by test:item-key-equal (item 1 ignored)
    ((item 1 first) (item 0 zero) (item 1 second)))
  ((item 0 zero)))
!(assertEqual
  (list:unique-by test:item-key-equal
    ((item 1 first) (item 0 zero) (item 1 second) (item 2 two)))
  ((item 1 first) (item 0 zero) (item 2 two)))
!(assertEqual (list:take 3 (a b c d e)) (a b c))
!(assertEqual (list:take 8 (a b c)) (a b c))
!(assertEqual (list:take 0 (a b c)) ())
!(assertEqual (list:take -2 (a b c)) ())
!(assertEqual
  (list:retain-top-k-keyed
    ((2 first) (3 second) (1 third) (3 fourth)) 3)
  ((2 first) (3 second) (3 fourth)))
!(assertEqual
  (list:retain-top-k-keyed
    ((2 first) (3 second) (3 third) (3 fourth)) 2)
  ((3 second) (3 third)))
!(assertEqual
  (list:retain-top-k-keyed ((2 first) (3 second)) 0)
  ())
!(assertEqual
  (list:retain-top-k-keyed ((2 first) (3 second)) 7)
  ((2 first) (3 second)))
!(assertEqual
  (list:key-by test:item-key
    ((item 2 first) (item 3 second) (item 1 third)))
  ((2 (item 2 first)) (3 (item 3 second)) (1 (item 1 third))))
!(assertEqual
  (list:unkey
    ((2 (item 2 first)) (3 (item 3 second)) (1 (item 1 third))))
  ((item 2 first) (item 3 second) (item 1 third)))
!(assertEqual (list:unkey ()) ())
; Malformed keyed data is a fault, not the dialect-dependent Empty sentinel.
!(assertEqualToResult
  (list:unkey ((malformed)))
  ((Error (unkey-atom ((malformed))) ExpectedKeyValuePair)))
!(assertEqual
  (list:retain-top-k-by-number test:item-key
    ((item 2 first) (item 3 second) (item 1 third) (item 3 fourth)) 3)
  ((item 2 first) (item 3 second) (item 3 fourth)))
!(assertEqual
  (list:retain-top-k-by-number test:item-key
    ((item 2 first) (item 3 second)) 0)
  ())
!(assertEqual
  (list:retain-top-k-by-number test:item-key
    ((item 2 first) (item 3 second)) 7)
  ((item 2 first) (item 3 second)))
!(assertEqual
  (list:retain-top-k-by-number test:item-key
    ((item 3 first) (item 3 second) (item 3 third)) 2)
  ((item 3 first) (item 3 second)))

; Free-monoid and constructor/eliminator laws on the expression domain.
!(assertEqual (list:append () (a b c)) (a b c))
!(assertEqual (list:append (a b c) ()) (a b c))
!(assertEqual
  (list:append (list:append (a b) (c)) (d e))
  (list:append (a b) (list:append (c) (d e))))
!(assertEqual (list:first (list:cons (nested x) (a b))) (nested x))
!(assertEqual (list:rest (list:cons (nested x) (a b))) (a b))
!(assertEqual (list:reverse (list:reverse (a (b c) d))) (a (b c) d))
!(assertEqual (list:len (list:reverse (a (b c) d))) 3)
!(assertEqual
  (list:reverse (list:append (a b) (c d)))
  (list:append (list:reverse (c d)) (list:reverse (a b))))
!(assertEqual (list:push (a b) c) (list:append (a b) (c)))
!(assertEqual (list:map (a b c) $x $x) (a b c))
!(assertEqual
  (list:map (list:map (1 2 3) $x (+ $x 1)) $y (* $y 2))
  (list:map (1 2 3) $x (* (+ $x 1) 2)))
!(assertEqual
  (list:foldl (list:append (1 2) (3 4)) 0 $acc $item (+ $acc $item))
  10)
!(assertEqual (list:len (list:range 2 9)) 7)
!(assertEqual (list:len (list:repeat 6 (compound value))) 6)
