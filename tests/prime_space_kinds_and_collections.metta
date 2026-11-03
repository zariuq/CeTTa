; Prime Space Kinds, Multi-Engine Selection, Brace-Bag Superpose,
; Canonical Collapse, Streaming Repeat, and Functional Dictionaries.

!(import! &self dict)

; ── 1. Prime { ... } Brace-Bag Superpose & Hyperpose (No Internal Tag Leak) ──
!(collapse-bag (superpose {c a b a}))
!(collapse-set (superpose {c a b a}))
!(collapse-bag (hyperpose {c a b a}))
!(collapse-bag (superpose {(repeat 3 a) (repeat 2 b)}))
!(collapse-set (superpose {(repeat 3 a) (repeat 2 b)}))

; ── 2. Set, Ordered-Set, Priority, and Stack Spaces in Prime ──
!(bind! &pset (new-space set native-candidate-exact))
!(get-type &pset)
!(add-atom &pset (item x))
!(add-atom &pset (item y))
!(add-atom &pset (item x))
!(space-len &pset)
!(collapse-bag (get-atoms &pset))

!(bind! &poset (new-space ordered-set))
!(get-type &poset)
!(add-atom &poset (step 1))
!(add-atom &poset (step 2))
!(add-atom &poset (step 1))
!(add-atom &poset (step 3))
!(space-len &poset)
!(space-pop &poset)
!(space-pop &poset)
!(space-pop &poset)

!(bind! &pprio (new-space priority))
!(get-type &pprio)
!(add-atom &pprio (goal 3 c))
!(add-atom &pprio (goal 1 a))
!(add-atom &pprio (goal 1 a))
!(add-atom &pprio (goal 2 b))
!(space-len &pprio)
!(space-pop &pprio)
!(space-pop &pprio)
!(space-pop &pprio)
!(space-pop &pprio)

; Stack LIFO top-removal law in Prime
!(bind! &pstack (new-space stack))
!(add-atom &pstack (mark 1))
!(add-atom &pstack (mark 2))
!(add-atom &pstack (mark 1))
!(remove-atom &pstack (mark 1))
!(space-pop &pstack)
!(space-pop &pstack)

; ── 3. Functional Dictionary in Prime (with Scope Migration) ──
!(bind! &pdict (dict:new))
!(dict:put! &pdict k1 100)
!(dict:put! &pdict k2 200)
!(dict:put-keep-first! &pdict k1 999)
!(dict:put-merge! &pdict k1 50 +)
!(dict:get &pdict k1)
!(dict:get-or &pdict k3 0)
!(dict:keys &pdict)
!(dict:entries &pdict)
!(dict:size &pdict)
!(dict:remove! &pdict k2)
!(dict:entries &pdict)

; ── 4. Prime List Operations, 2000-Item Folds/add-atoms, and Stream fold+match ──
!(foldl-atom [1 2 3 4] 0 $acc $x (+ $acc $x))
!(foldl-atom [] 0 $acc $x (+ $acc $x))
!(map-atom [1 2 3] $x (* $x 10))
!(map-atom [] $x (* $x 10))
!(filter-atom [1 2 3 4] $x (> $x 2))
!(filter-atom [] $x (> $x 2))

!(bind! &pbatch (new-space set))
!(let $r (range-atom 2000) (let $kv (map-atom $r $x (row $x)) (add-atoms &pbatch $kv)))
!(space-len &pbatch)

!(bind! &pfold-s (new-space set))
!(let $r (range-atom 2000) (foldl-atom $r 0 $acc $x (let $_ (add-atom &pfold-s (row $x)) (+ $acc 1))))
!(space-len &pfold-s)

(= (stream-range $lo $hi) (if (< $lo $hi) $lo (empty)))
(= (stream-range $lo $hi) (if (< (+ $lo 1) $hi) (stream-range (+ $lo 1) $hi) (empty)))
!(fold (stream-range 0 100) 0 $acc $i (let $v (match &pdict (dict:entry k1 $w) $w) (+ $acc $v)))

