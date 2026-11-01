; Pure derived properties of stored syntax, using the existing native memo.
!(import! &self "space_curriculum.metta")
!(import! &self (library lib_memo))

(QueryPureCallbackV1 space-computed:syntax-row?)
; The annotation's memo-policy read is covered by the query's program watch.
; Other mutable configuration needs its own captured observation contract.
(QueryPureCallbackV1 space-computed:annotate)

; The argument contains every data dependency. Neither this function nor its
; callees read a mutable Space, consult a clock or perform application effects.
(: space-computed:nodes (-> Atom %Undefined%))
(: space-computed:nodes-reference (-> Atom %Undefined%))
(= (space-computed:leaf-weight) 1)
(= (space-computed:nodes $body) (space-computed:count-node space-computed:nodes $body))
(= (space-computed:nodes-reference $body) (space-computed:count-node space-computed:nodes-reference $body))
(: space-computed:count-node (-> Atom Atom %Undefined%))
(= (space-computed:count-node $recur $body)
  (case $body
    (((Use $module) (space-computed:leaf-weight))
     ((Seq $left $right)
      (let* (($a ($recur $left)) ($b ($recur $right)))
        (if (and (kb:integer? $a) (kb:integer? $b))
          (+ 1 (+ $a $b)) ComputedSyntaxRefusedV1)))
     ($invalid ComputedSyntaxRefusedV1))))
!(memoize space-computed:nodes)

; Check ground stored rows before schema binding, so open data cannot invent
; syntax or seed a memo entry. Unknown ground syntax has an explicit result.
(= (space-computed:syntax-row? (SpaceAnswerV1 $fact $references))
  (if (and (knowledge:ground-identity? $fact)
           (knowledge:tagged? $fact program-syntax 4)) RelTrue RelFalse))
(= (space-computed:annotate
    (SpaceAnswerV1 (program-syntax $syntax $program $body) $references))
  (let $count (if (space-computed:policy-compatible?)
                 (space-computed:nodes $body) (space-computed:nodes-reference $body))
    (ComputedProgramV1 $syntax $program
      (if (== $count ComputedSyntaxRefusedV1) (ComputedSyntaxRefusedV1 $body) $count) $references)))
; Do not silently change the caller's global memo settings. In particular,
; aggregate=count would count answers rather than return a syntax-node count.
; Use the uncached MeTTa reference when the policy changes the return value.
(= (space-computed:policy-compatible?)
  (case (get-memoize-config)
    ((((strategy $s) (unique-limit $n) (size-limit $b) (float $f)
       (answer-limit $a) (aggregate none)) True)
     ($other False))))
(= (space-computed:plan)
  (KqMap
    (KqSelect (KqSpace curriculum $fact $fact) space-computed:syntax-row?)
    space-computed:annotate))

; emit's two equal memberships remain distinct answers even though both use
; the same cached syntax count. Seq counts one node plus its two children.
;   emit: Seq -- Use parse       check: Use parse
;           `-- Use parse
(= (space-computed:install $path)
  (case (space-curriculum:install $path)
    (((KnowledgeCommittedV1 $answer $epoch $revision) (space-computed:seed $path))
     ((KnowledgeReplayedV1 $answer) (space-computed:seed $path))
     ($failure $failure))))
(= (space-computed:seed $path)
  (case (store:open $path "existing")
    (((StoreOpened $owner)
      (let $answer
        (case (store:snapshot $owner)
          (((StoreSnapshot $view $epoch $revision)
            (let* (($committed (durable-space:commit $owner $view curriculum computed-syntax-v1
                     ((SpaceInsertV1 0 (program-syntax syntax1 emit (Seq (Use parse) (Use parse))))
                      (SpaceInsertV1 1 (program-syntax syntax1 emit (Seq (Use parse) (Use parse))))
                      (SpaceInsertV1 2 (program-syntax syntax2 check (Use parse))))))
                   ($_ (store:close $view))) $committed))
           ($failure $failure)))
        (let $_ (store:close $owner) $answer)))
     ($failure $failure))))
(= (space-computed:configuration) (space-curriculum:configuration))
(= (space-computed:run (Install $path)) (space-computed:install $path))
(= (space-computed:run Plan) (space-computed:plan))
(= (space-computed:run Stats) (get-memoize-stats))
(= (space-computed:run (Query $path $count $fuel))
  (space-curriculum:run (Query $path (space-computed:plan) $count $fuel)))
