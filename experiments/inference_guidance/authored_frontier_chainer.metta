; Depth-bounded proof search authored in ordinary MeTTa.  Declarations live in
; an explicitly ordered Space; each clause matches the known conclusion before
; recursively deriving its premises.

; Prime quotation keeps synthesized proof syntax inert until it reaches the
; kernel.  These constructor shapes are deliberately explicit: reconstructing
; nested dependent terms through a generic cons-atom fold exposes child
; applications to ordinary evaluation before the enclosing term is checked.
(= (proof:build (proof:constructor $name ()) ())
   (quote $name))
(= (proof:build (proof:constructor $name ()) ((quote $p1)))
   (quote ($name $p1)))
(= (proof:build (proof:constructor $name ())
                ((quote $p1) (quote $p2)))
   (quote ($name $p1 $p2)))
(= (proof:build (proof:constructor $name ())
                ((quote $p1) (quote $p2) (quote $p3)))
   (quote ($name $p1 $p2 $p3)))

(= (proof:build (proof:constructor $name ($a1)) ())
   (quote ($name $a1)))
(= (proof:build (proof:constructor $name ($a1)) ((quote $p1)))
   (quote ($name $a1 $p1)))
(= (proof:build (proof:constructor $name ($a1))
                ((quote $p1) (quote $p2)))
   (quote ($name $a1 $p1 $p2)))
(= (proof:build (proof:constructor $name ($a1))
                ((quote $p1) (quote $p2) (quote $p3)))
   (quote ($name $a1 $p1 $p2 $p3)))

(= (proof:build (proof:constructor $name ($a1 $a2)) ())
   (quote ($name $a1 $a2)))
(= (proof:build (proof:constructor $name ($a1 $a2)) ((quote $p1)))
   (quote ($name $a1 $a2 $p1)))
(= (proof:build (proof:constructor $name ($a1 $a2))
                ((quote $p1) (quote $p2)))
   (quote ($name $a1 $a2 $p1 $p2)))
(= (proof:build (proof:constructor $name ($a1 $a2))
                ((quote $p1) (quote $p2) (quote $p3)))
   (quote ($name $a1 $a2 $p1 $p2 $p3)))

(= (proof:build (proof:constructor $name ($a1 $a2 $a3)) ())
   (quote ($name $a1 $a2 $a3)))
(= (proof:build (proof:constructor $name ($a1 $a2 $a3))
                ((quote $p1)))
   (quote ($name $a1 $a2 $a3 $p1)))
(= (proof:build (proof:constructor $name ($a1 $a2 $a3))
                ((quote $p1) (quote $p2)))
   (quote ($name $a1 $a2 $a3 $p1 $p2)))
(= (proof:build (proof:constructor $name ($a1 $a2 $a3))
                ((quote $p1) (quote $p2) (quote $p3)))
   (quote ($name $a1 $a2 $a3 $p1 $p2 $p3)))

(= (proof:build (proof:constructor $name ($a1 $a2 $a3 $a4)) ())
   (quote ($name $a1 $a2 $a3 $a4)))
(= (proof:build (proof:constructor $name ($a1 $a2 $a3 $a4))
                ((quote $p1)))
   (quote ($name $a1 $a2 $a3 $a4 $p1)))
(= (proof:build (proof:constructor $name ($a1 $a2 $a3 $a4))
                ((quote $p1) (quote $p2)))
   (quote ($name $a1 $a2 $a3 $a4 $p1 $p2)))
(= (proof:build (proof:constructor $name ($a1 $a2 $a3 $a4))
                ((quote $p1) (quote $p2) (quote $p3)))
   (quote ($name $a1 $a2 $a3 $a4 $p1 $p2 $p3)))

; Transparent reference storage for the ordinary recursive relation below.
(= (proof:load-reference-space $space $declarations)
   (if (== $declarations ())
       ()
       (let $pair (decons-atom $declarations)
         (unify ($declaration $remaining) $pair
           (let $_ (add-atom $space $declaration)
             (proof:load-reference-space $space $remaining))
           (proof:invalid-candidate-list $declarations)))))

; Populate an ordered Space from only the requested prefix of a ranked list.
; The bound is a readout decision: ranking itself still preserves every
; occurrence and its stable order.
(= (proof:load-reference-prefix $space $limit $declarations)
   (if (<= $limit 0)
       ()
       (if (== $declarations ())
           ()
           (let $pair (decons-atom $declarations)
             (unify ($declaration $remaining) $pair
               (let $_ (add-atom $space $declaration)
                 (proof:load-reference-prefix
                   $space (- $limit 1) $remaining))
               (proof:invalid-candidate-list $declarations))))))

; Uniform authored relation: facts and each supported Horn arity are separate
; clauses.  This leaves indexing, freshening, and branch substitutions to the
; ordinary MeTTa relation engine instead of encoding an arity dispatcher in a
; proof-search primitive.
(= (proof:derive $space $_ $goal)
   (match $space
     (: $rule-data $goal)
     (proof:tree $rule-data ())))

(= (proof:derive $space (S $depth) $goal)
   (match $space
     (: $rule-data (-> $premise1 $goal))
     (chain
       (proof:derive $space $depth $premise1)
       $proof1
       (proof:tree $rule-data ($proof1)))))

(= (proof:derive $space (S $depth) $goal)
   (match $space
     (: $rule-data (-> $premise1 $premise2 $goal))
     (chain
       (proof:derive $space $depth $premise1)
       $proof1
       (chain
         (proof:derive $space $depth $premise2)
         $proof2
         (proof:tree $rule-data ($proof1 $proof2))))))

(= (proof:derive $space (S $depth) $goal)
   (match $space
     (: $rule-data (-> $premise1 $premise2 $premise3 $goal))
     (chain
       (proof:derive $space $depth $premise1)
       $proof1
       (chain
         (proof:derive $space $depth $premise2)
         $proof2
         (chain
           (proof:derive $space $depth $premise3)
           $proof3
           (proof:tree $rule-data ($proof1 $proof2 $proof3)))))))

(= (proof:reify (proof:tree $rule-data ()))
   (proof:build $rule-data ()))

(= (proof:reify (proof:tree $rule-data ($tree1)))
   (let $proof1 (proof:reify $tree1)
     (proof:build $rule-data ($proof1))))

(= (proof:reify (proof:tree $rule-data ($tree1 $tree2)))
   (let* (($proof1 (proof:reify $tree1))
          ($proof2 (proof:reify $tree2)))
     (proof:build $rule-data ($proof1 $proof2))))

(= (proof:reify (proof:tree $rule-data ($tree1 $tree2 $tree3)))
   (let* (($proof1 (proof:reify $tree1))
          ($proof2 (proof:reify $tree2))
          ($proof3 (proof:reify $tree3)))
     (proof:build $rule-data ($proof1 $proof2 $proof3))))
