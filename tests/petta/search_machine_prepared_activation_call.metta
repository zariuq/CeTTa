; A determinate helper inside a nondeterminate caller shares the compiled
; call boundary without collapsing the caller's order or multiplicity.
(= (activation-choice) 1)
(= (activation-choice) 1)
(= (activation-choice) 2)
(= (activation-sum $items $acc)
   (if (== $items Nil) $acc
       (let (Cons $head $tail) $items
         (activation-sum $tail (+ $acc $head)))))
(= (activation-driver)
   (let $n (activation-choice)
     (activation-sum (Cons $n (Cons 10 Nil)) 0)))
!(activation-driver)

; An open input must still bind the caller's variable through normal search.
(= (activation-refine (Box A)) accepted)
(= (activation-open $item)
   (let $answer (activation-refine $item) (answer $item $answer)))
!(activation-open (Box $v))

; Repeated-variable failure must not leak a partial binding to a sibling.
(= (activation-pair) (Pair A B))
(= (activation-pair) (Pair C C))
(= (activation-repeated)
   (let (Pair $x $x) (activation-pair)
     (activation-sum (Cons 3 Nil) 0)))
!(activation-repeated)

; A nested multi-answer helper remains relational, including duplicates.
(= (activation-multiple $x) same)
(= (activation-multiple $x) same)
(= (activation-multiple-driver)
   (let $n (activation-choice) (activation-multiple $n)))
!(collapse (activation-multiple-driver))

; Effects cannot be absorbed into a determinate pure execution segment.
(= (activation-touch $x)
   (let $ignored (add-atom &self (activation-seen $x)) $x))
(= (activation-effects)
   (let $n (activation-choice) (activation-touch $n)))
!(collapse (activation-effects))
!(collapse (match &self (activation-seen $x) $x))

; A compiled helper is subordinate to the current Space revision. Adding a
; second equation restores its alternatives rather than reusing a singleton.
(= (activation-revision $x) old)
(= (activation-mutate)
   (let $before (activation-revision 0)
     (let $ignored (add-atom &self (= (activation-revision $x) new))
       (pair $before (activation-revision 0)))))
!(activation-mutate)
