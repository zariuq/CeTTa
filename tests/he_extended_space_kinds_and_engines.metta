; Space Kinds, Multi-Engine Selection, Canonical Collapse, Streaming Repeat,
; and Functional Dictionaries in CeTTa (HE Extended Profile).

!(import! &self dict)

; ── 1. Set Space: duplicate suppression and order-insensitive membership ──
!(bind! &set-native (new-space set))
!(get-type &set-native)
!(add-atom &set-native (edge a b))
!(add-atom &set-native (edge b c))
!(add-atom &set-native (edge a b))
!(space-len &set-native)
!(collapse-bag (get-atoms &set-native))
!(remove-atom &set-native (edge a b))
!(space-len &set-native)

; Set space backed by native-candidate-exact engine via 2-arg (new-space <kind> <engine>)
!(bind! &set-nce (new-space set native-candidate-exact))
!(get-type &set-nce)
!(add-atom &set-nce (fact p))
!(add-atom &set-nce (fact q))
!(add-atom &set-nce (fact p))
!(space-len &set-nce)
!(collapse-bag (match &set-nce (fact $x) $x))

; ── 2. Ordered-Set Space (oset): FIFO insertion order + duplicate suppression ──
; Motivating example: BFS frontier / tabling queue where re-discovered states
; are ignored without disturbing the FIFO position of first discovery.
!(bind! &frontier (new-space oset))
!(get-type &frontier)
!(add-atom &frontier (state s0))
!(add-atom &frontier (state s1))
!(add-atom &frontier (state s0))
!(add-atom &frontier (state s2))
!(add-atom &frontier (state s1))
!(space-len &frontier)
!(get-atoms &frontier)
!(space-peek &frontier)
!(space-pop &frontier)
!(space-pop &frontier)
!(space-pop &frontier)
!(assertEqualToResult (space-pop &frontier) ((Error (space-pop &frontier) EmptyOrderedSetSpace)))
!(assertEqualToResult (space-peek &frontier) ((Error (space-peek &frontier) EmptyOrderedSetSpace)))

; ── 3. Priority Space: canonical shortlex min-queue ──
; Motivating example: deterministic best-first agenda where (space-pop)
; always extracts the canonical minimum item regardless of insertion order.
!(bind! &agenda (new-space priority native-candidate-exact))
!(get-type &agenda)
!(add-atom &agenda (task 30 render))
!(add-atom &agenda (task 10 parse))
!(add-atom &agenda (task 20 check))
!(add-atom &agenda (task 10 lex))
!(add-atom &agenda (task 10 parse))
!(space-len &agenda)
!(space-peek &agenda)
!(space-pop &agenda)
!(space-pop &agenda)
!(space-pop &agenda)
!(space-pop &agenda)
!(space-pop &agenda)
!(assertEqualToResult (space-pop &agenda) ((Error (space-pop &agenda) EmptyPrioritySpace)))
!(assertEqualToResult (space-peek &agenda) ((Error (space-peek &agenda) EmptyPrioritySpace)))

; ── 4. Stack Space: LIFO top-removal law (lifoStack_remove_eq_pop) ──
; Motivating example: scoped backtracking trail where (frame a) is pushed,
; then (frame b), then a nested (frame a). Removing (frame a) must remove the
; TOP (most recent) frame, leaving the older bottom (frame a) intact.
!(bind! &trail (new-space stack))
!(add-atom &trail (frame a))
!(add-atom &trail (frame b))
!(add-atom &trail (frame a))
!(get-atoms &trail)
!(remove-atom &trail (frame a))
!(get-atoms &trail)
!(space-pop &trail)
!(space-pop &trail)
!(assertEqualToResult (space-pop &trail) ((Error (space-pop &trail) EmptyStackSpace)))

; ── 5. Engine discipline and engine query invariance ──
!(assertEqualToResult (new-space set nope-engine) ((Error (new-space set nope-engine) UnknownSpaceEngine)))
!(assertEqualToResult (new-space nope-kind native) ((Error (new-space nope-kind native) UnknownSpaceKind)))

; Engine query invariance across native and native-candidate-exact.
!(bind! &rel-nat (new-space atom native))
!(bind! &rel-nce (new-space atom native-candidate-exact))
!(add-atoms &rel-nat ((rel a 1) (rel b 2) (rel a 3) (rel a 1)))
!(add-atoms &rel-nce ((rel a 1) (rel b 2) (rel a 3) (rel a 1)))
!(== (collapse-bag (match &rel-nat (rel a $v) $v))
     (collapse-bag (match &rel-nce (rel a $v) $v)))
!(== (collapse-set (match &rel-nat (rel a $v) $v))
     (collapse-set (match &rel-nce (rel a $v) $v)))

; ── 6. Canonical Reification: collapse-bag and collapse-set ──
!(collapse-bag (superpose (c a b a c a)))
!(collapse-set (superpose (c a b a c a)))
!(== (collapse-bag (superpose (z x y x)))
     (collapse-bag (superpose (x z x y))))
!(== (collapse-set (superpose (z x y x z)))
     (collapse-set (superpose (y x z))))

; Alpha-equivalent canonical deduplication in collapse-set is permutation-invariant.
!(== (collapse-set (superpose ((rule $x $x) (rule $y $y))))
     (collapse-set (superpose ((rule $y $y) (rule $x $x)))))
!(let $s (collapse-set (superpose ((rule $x $x) (rule $y $y)))) (size-atom $s))

; ── 7. Streaming Iterator: (repeat $n $expr) ──
; Multiplicity construction for bags and side-effecting iteration.
!(collapse-bag (repeat 4 a))
!(collapse-bag (superpose ((repeat 3 a) (repeat 2 b) (repeat 1 c))))
!(collapse-set (superpose ((repeat 3 a) (repeat 2 b) (repeat 1 c))))

!(bind! &counter-bag (new-space atom))
!(bind! &counter-set (new-space set))
!(collapse (repeat 0 (add-atom &counter-bag should-not-run)))
!(space-len &counter-bag)
!(collapse (repeat 3 (add-atom &counter-bag tick)))
!(collapse (repeat 3 (add-atom &counter-set tick)))
!(space-len &counter-bag)
!(space-len &counter-set)
!(collapse-bag (repeat -1 a))

; ── 8. Functional Dictionary Library (lib/dict.metta) ──
; Motivating example: event histogram with replace, keep-first, reject, and merge.
!(bind! &hist (dict:new-with native-candidate-exact))
!(dict:put! &hist alpha 10)
!(dict:put! &hist beta 20)
!(dict:put! &hist alpha 15)
!(dict:get &hist alpha)
!(dict:get-or &hist gamma 0)
!(dict:has-key? &hist beta)
!(dict:has-key? &hist gamma)

; keep-first preserves existing binding, inserts missing key
!(dict:put-keep-first! &hist alpha 999)
!(dict:put-keep-first! &hist gamma 5)
!(dict:get &hist alpha)
!(dict:get &hist gamma)

; reject raises DuplicateDictKey on existing key
!(assertEqualToResult
  (dict:put-reject! &hist alpha 100)
  ((Error (dict:put-reject! &hist alpha 100) (DuplicateDictKey alpha))))

; merge combines old and new values with binary operator
!(dict:put-merge! &hist alpha 5 +)
!(dict:put-merge! &hist delta 7 +)
!(dict:get &hist alpha)
!(dict:get &hist delta)
!(dict:keys &hist)
!(dict:entries &hist)
!(dict:size &hist)
!(dict:remove! &hist beta)
!(dict:entries &hist)

; Functional invariant check: raw duplicate keys trigger AmbiguousDictKey
!(add-atom &hist (dict:entry dup 1))
!(add-atom &hist (dict:entry dup 2))
!(assertEqualToResult
  (dict:get &hist dup)
  ((Error (dict:get &hist dup) (AmbiguousDictKey dup))))

; ── 9. Motivating Example: Deduped BFS Frontier on a Cyclic Graph ──
!(bind! &graph (new-space set))
!(add-atoms &graph ((arc n0 n1) (arc n0 n2) (arc n1 n2) (arc n2 n0) (arc n2 n3) (arc n0 n1)))
!(space-len &graph)
!(bind! &bfs-q (new-space ordered-set))
!(add-atom &bfs-q n0)
!(collapse (let $u (match &graph (arc n0 $v) $v) (add-atom &bfs-q $u)))
!(collapse (let $u (match &graph (arc n0 $v) (match &graph (arc $v $w) $w)) (add-atom &bfs-q $u)))
!(space-len &bfs-q)
!(space-pop &bfs-q)
!(space-pop &bfs-q)
!(space-pop &bfs-q)
!(space-pop &bfs-q)

; ── 10. [ ... ] List Operations (foldl-atom, map-atom, filter-atom, add-atoms) ──
!(foldl-atom [1 2 3 4] 0 $acc $x (+ $acc $x))
!(foldl-atom [] 0 $acc $x (+ $acc $x))
!(map-atom [1 2 3] $x (* $x 10))
!(map-atom [] $x (* $x 10))
!(filter-atom [1 2 3 4] $x (> $x 2))
!(filter-atom [] $x (> $x 2))
!(bind! &list-s (new-space set))
!(add-atoms &list-s [(item a) (item b) (item a)])
!(space-len &list-s)
!(collapse-bag (get-atoms &list-s))


