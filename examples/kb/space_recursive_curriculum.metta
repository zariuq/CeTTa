; Recursive derived graphs cache only a complete immutable edge projection.
; A Space handle or external lookup never enters the memo key.
!(import! &self (library db))
!(import! &self (library lib_memo))
!(import! &self (library list))

(= (space-recursive:neighbors $edges $node)
  (if (== $edges ()) ()
    (let* ((($edge $rest) (decons-atom $edges))
           ($tail (space-recursive:neighbors $rest $node)))
      (case $edge
        (((Edge $node $target) (cons-atom $target $tail))
         ($other $tail))))))

(= (space-recursive:member? $value $items)
  (if (== $items ()) False
    (let ($head $tail) (decons-atom $items)
      (if (== $value $head) True (space-recursive:member? $value $tail)))))

(= (space-recursive:add-new $values $seen $reversed)
  (if (== $values ()) (RecursiveNewV1 $seen (list:reverse $reversed))
    (let ($value $rest) (decons-atom $values)
      (if (space-recursive:member? $value $seen)
        (space-recursive:add-new $rest $seen $reversed)
        (space-recursive:add-new $rest (cons-atom $value $seen)
          (cons-atom $value $reversed))))))

(= (space-recursive:walk $edges $frontier $seen)
  (if (== $frontier ()) $seen
    (let* ((($node $rest) (decons-atom $frontier))
           ((RecursiveNewV1 $next-seen $new)
             (space-recursive:add-new (space-recursive:neighbors $edges $node) $seen ())))
      (space-recursive:walk $edges (list:append $rest $new) $next-seen))))

(= (space-recursive:reach $edges $start)
  (if (and (== (get-metatype $edges) Expression)
           (knowledge:ground-identity? $edges))
    (list:reverse (space-recursive:walk $edges ($start) ()))
    ComputedGraphRefusedV1))
!(memoize space-recursive:reach)

; An acyclic projection can use recursive memo forwarding directly. The
; diamond fixture reaches d/e through both branches, demonstrating shared hits.
(= (space-recursive:dag $edges $start)
  (if (and (== (get-metatype $edges) Expression)
           (knowledge:ground-identity? $edges))
    (let $children (space-recursive:neighbors $edges $start)
      (unique-atom (space-recursive:dag-children $edges $children $children)))
    ComputedGraphRefusedV1))
(= (space-recursive:dag-children $edges $pending $answers)
  (if (== $pending ()) $answers
    (let ($node $rest) (decons-atom $pending)
      (space-recursive:dag-children $edges $rest
        (list:append $answers (space-recursive:dag $edges $node))))))
!(memoize space-recursive:dag)

(= (space-recursive:edges $space)
  (collapse (match $space (edge $from $to) (Edge $from $to))))

(= (space-recursive:seed $space)
  (let* (($_ (add-atom $space (edge a b)))
         ($_ (add-atom $space (edge a c)))
         ($_ (add-atom $space (edge b d)))
         ($_ (add-atom $space (edge c d)))
         ($_ (add-atom $space (edge d e)))
         ($_ (add-atom $space (edge e c)))) RecursiveSeededV1))

(= (space-recursive:run (Install $path))
  (let* (($db (db:open $path))
         ($_ (db:ensure $db graph ()))
         ($answer (db:transaction-result $db graph recursive-seed-v1 seed-v1
           (|-> ($space) (space-recursive:seed $space))))
         ($_ (db:close $db))) $answer))
(= (space-recursive:run (Edges $path))
  (let* (($db (db:open $path)) ($space (db:snapshot $db graph))
         ($edges (space-recursive:edges $space))
         ($_ (db:close $space)) ($_ (db:close $db))) $edges))
(= (space-recursive:run (Reach $edges $start)) (space-recursive:reach $edges $start))
(= (space-recursive:run (Dag $edges $start)) (space-recursive:dag $edges $start))
(= (space-recursive:run (Add $path $from $to $request))
  (let* (($db (db:open $path))
         ($answer (db:transaction-result $db graph $request (edge $from $to)
           (|-> ($space) (let $_ (add-atom $space (edge $from $to)) RecursiveEdgeAddedV1))))
         ($_ (db:close $db))) $answer))
(= (space-recursive:run Stats) (get-memoize-stats))
(= (space-recursive:run ClearStats) (clear-memoize-stats))
(= (space-recursive:run (Reject $value)) (space-recursive:reach $value a))
