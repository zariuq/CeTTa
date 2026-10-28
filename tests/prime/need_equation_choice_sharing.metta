; One source suspension is shared across strict and wildcard equation
; candidates.  Its effect occurs once even though the demanded choice has two
; outcomes and the wildcard result later observes the same suspension.
!(bind! &prime-need:choice-state (new-state 0))
(= (prime-need:choice-source)
   (let $old (get-state &prime-need:choice-state)
     (let $_ (change-state! &prime-need:choice-state (+ $old 1))
       (superpose (A B)))))
(= (prime-need:choice A) (left A))
(= (prime-need:choice B) (right B))
(= (prime-need:choice $x) (wild $x))
!(prime-need:choice (prime-need:choice-source))
!(get-state &prime-need:choice-state)

; Explicit evaluation remains inside the shared computation. Each matching
; rule receives its answer; adding readers does not repeat the effect.
(= (prime-need:eval-tick) (let $_ (println! shared-eval-effect) 7))
(= (prime-need:eval-producer) (eval (prime-need:eval-tick)))
(= (prime-need:eval-use 7) Matched)
(= (prime-need:eval-use $x) (Any $x))
!(prime-need:eval-use (prime-need:eval-producer))

(= (prime-need:three-tick) (let $_ (println! shared-three-effect) 7))
(= (prime-need:three-producer) (eval (prime-need:three-tick)))
(= (prime-need:three-use 7) First)
(= (prime-need:three-use 7) Second)
(= (prime-need:three-use $x) (Any $x))
!(prime-need:three-use (prime-need:three-producer))

; A scoped iteration may call effectful code. Each element's callback runs
; once even when two equation readers consume the shared result.
(= (prime-need:map-step $x) (let $_ (println! (map-earned $x)) $x))
(= (prime-need:map-source) (map-atom (1 2) $x (prime-need:map-step $x)))
(= (prime-need:map-use (1 2)) MapMatched)
(= (prime-need:map-use $x) (MapAny $x))
!(prime-need:map-use (prime-need:map-source))

(= (prime-need:fold-step $a $x) (let $_ (println! (fold-earned $x)) (+ $a $x)))
(= (prime-need:fold-source) (foldl-atom (1 2) 0 $a $x (prime-need:fold-step $a $x)))
(= (prime-need:fold-use 3) FoldMatched)
(= (prime-need:fold-use $x) (FoldAny $x))
!(prime-need:fold-use (prime-need:fold-source))

; The relational map fallback destructures each item before an effectful
; callback. Independent rule readers still share the same production.
(= (prime-need:tuple-map-step $x)
   (let $_ (println! (map-fallback $x)) (/ $x 2)))
(= (prime-need:tuple-map-source $pairs)
   (map-atom $pairs $item
     (let ($name $value) $item ($name (prime-need:tuple-map-step $value)))))
(= (prime-need:tuple-map-use ((healthy 1) (faulty 2))) TupleMatched)
(= (prime-need:tuple-map-use $x) (TupleAny $x))
!(prime-need:tuple-map-use
   (prime-need:tuple-map-source ((healthy 2) (faulty 4))))
