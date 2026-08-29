!(import! &self clist)

!(assertEqual (clist:is-empty (clist:nil)) True)
!(assertEqual (clist:is-cons (clist:cons a (clist:nil))) True)
!(assertEqual (clist:first (clist:cons a (clist:nil))) a)
!(assertEqual (clist:rest (clist:cons a (clist:nil))) (clist:nil))
!(assertEqual (clist:to-list (clist:from-list (a b c))) (a b c))
!(assertEqual (clist:len (clist:from-list (a b c))) 3)
!(assertEqual
  (clist:append (clist:from-list (a b)) (clist:from-list (c d)))
  (clist:from-list (a b c d)))
!(assertEqual
  (clist:reverse (clist:from-list (a b c)))
  (clist:from-list (c b a)))
!(assertEqual
  (clist:map (clist:from-list (1 2 3)) $x (+ $x 1))
  (clist:from-list (2 3 4)))
!(assertEqual
  (clist:filter (clist:from-list (1 2 3 4)) $x (> $x 2))
  (clist:from-list (3 4)))
!(assertEqual
  (clist:foldl (clist:from-list (1 2 3 4)) 0 $acc $item (+ $acc $item))
  10)
!(assertEqual (clist:range 5) (clist:from-list (0 1 2 3 4)))
!(assertEqual (clist:range 2 6) (clist:from-list (2 3 4 5)))
!(assertEqual (clist:range 4 1) (clist:nil))
!(assertEqual (clist:repeat 3 z) (clist:from-list (z z z)))
!(assertEqual (clist:repeat 0 z) (clist:nil))

; The inductive and expression presentations are inverse on their domains.
!(assertEqual
  (clist:to-list (clist:from-list (a (b c) d)))
  (a (b c) d))
!(assertEqual
  (clist:from-list
    (clist:to-list
      (clist:cons a (clist:cons (b c) (clist:cons d (clist:nil))))))
  (clist:cons a (clist:cons (b c) (clist:cons d (clist:nil)))))

; Free-monoid laws and representation-preservation laws.
!(assertEqual
  (clist:append (clist:nil) (clist:from-list (a b)))
  (clist:from-list (a b)))
!(assertEqual
  (clist:append (clist:from-list (a b)) (clist:nil))
  (clist:from-list (a b)))
!(assertEqual
  (clist:append
    (clist:append (clist:from-list (a)) (clist:from-list (b c)))
    (clist:from-list (d e)))
  (clist:append
    (clist:from-list (a))
    (clist:append (clist:from-list (b c)) (clist:from-list (d e)))))
!(assertEqual
  (clist:reverse (clist:reverse (clist:from-list (a (b c) d))))
  (clist:from-list (a (b c) d)))
!(assertEqual
  (clist:len (clist:reverse (clist:from-list (a b c d))))
  4)
!(assertEqual
  (clist:to-list
    (clist:append (clist:from-list (a b)) (clist:from-list (c d))))
  (list:append (a b) (c d)))
!(assertEqual
  (clist:to-list (clist:reverse (clist:from-list (a b c))))
  (list:reverse (a b c)))
!(assertEqual
  (clist:len (clist:from-list (a b c d e)))
  (list:len (a b c d e)))
!(assertEqual
  (clist:to-list
    (clist:map (clist:from-list (1 2 3)) $x (+ $x 1)))
  (list:map (1 2 3) $x (+ $x 1)))
!(assertEqual
  (clist:to-list
    (clist:filter (clist:from-list (1 2 3 4)) $x (== (% $x 2) 0)))
  (list:filter (1 2 3 4) $x (== (% $x 2) 0)))
!(assertEqual
  (clist:foldl (clist:from-list (1 2 3 4)) 10 $acc $item (+ $acc $item))
  (list:foldl (1 2 3 4) 10 $acc $item (+ $acc $item)))
!(assertEqual
  (clist:to-list (clist:range 2 7))
  (list:range 2 7))
!(assertEqual
  (clist:to-list (clist:repeat 4 (compound value)))
  (list:repeat 4 (compound value)))

; A represented fold may erase only producers named by the generated
; execution contract.  Unlisted producers take the ordinary materialize-then-
; fold path, preserving alternatives, duplicate occurrences, Empty, and
; effects exactly once.
(= (clist:test-produced) (clist:from-list (1 2 3)))
!(assertEqual
  (clist:foldl (clist:test-produced) 0 $acc $item (+ $acc $item))
  6)

(= (clist:test-alternatives) (clist:from-list (1 2)))
(= (clist:test-alternatives) (clist:from-list (3 4)))
!(assertEqual
  (collapse
    (clist:foldl (clist:test-alternatives) 0 $acc $item (+ $acc $item)))
  (3 7))

(= (clist:test-duplicates) (clist:from-list (1 2)))
(= (clist:test-duplicates) (clist:from-list (1 2)))
!(assertEqual
  (collapse
    (clist:foldl (clist:test-duplicates) 0 $acc $item (+ $acc $item)))
  (3 3))

(= (clist:test-empty-or-value) (empty))
(= (clist:test-empty-or-value) (clist:from-list (1 2)))
!(assertEqual
  (collapse
    (clist:foldl (clist:test-empty-or-value) 0 $acc $item (+ $acc $item)))
  (3))

!(bind! &clist:test-fold-effects (new-space))
(= (clist:test-effectful)
   (let $_receipt
        (add-atom &clist:test-fold-effects tick)
     (clist:from-list (1 2))))
!(assertEqual
  (clist:foldl (clist:test-effectful) 0 $acc $item (+ $acc $item))
  3)
!(assertEqual
  (collapse (match &clist:test-fold-effects $effect $effect))
  (tick))

(= (clist:test-malformed) (ClistCons 1 malformed))
!(assertEqual
  (clist:foldl (clist:test-malformed) 0 $acc $item (+ $acc $item))
  (list:foldl
    (clist:to-list (clist:test-malformed))
    0 $acc $item (+ $acc $item)))

; Partial eliminators remain inert on nil or malformed inductive data.
!(clist:first ClistNil)
!(clist:rest ClistNil)
!(clist:len (ClistCons a malformed))
!(clist:to-list (ClistCons a malformed))

; The represented consumer expansion is licensed only while the imported
; definition is its sole applicable equation.  An added alternative must
; disable the optimization and preserve the ordinary result bag.
(= (clist:foldl $list $init $acc $item $step)
   extra-fold-result)
!(assertEqual
  (collapse
    (clist:foldl
      (clist:from-list (1 2))
      0 $acc $item (+ $acc $item)))
  (3 extra-fold-result))
