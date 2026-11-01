; A persistent ordinary-Space application. The same MeTTa plans run embedded
; and through the local server. Data files are synthetic teaching examples.
!(import! &self (library kb_query))
!(import! &self (library kb_graph))
!(import! &self "claims_data.metta")
!(import! &self "dependencies_data.metta")
!(import! &self "../native_space_query_server.metta")
!(import! &self "../native_space_graph_server.metta")

(QueryPureCallbackV1 kb:alpha-equal)
(QueryPureCallbackV1 space-curriculum:origin)
(QueryPureCallbackV1 space-curriculum:count)

; Logical application identifiers occur in the payload. StoreOccurrenceV1
; identifies one membership occurrence independently of equal payloads.
(= (space-curriculum:edits)
  (let $rows (collapse (match &self (KbFact $id $relation $fields)
    (SpaceInsertV1 $id (cons-atom $relation (cons-atom $id $fields)))))
    (rel:order space-curriculum:label-less $rows)))
(= (space-curriculum:label-less (SpaceInsertV1 $left $a) (SpaceInsertV1 $right $b))
  (let* (((StoreEncoded $x) (store:encode $left)) ((StoreEncoded $y) (store:encode $right)))
    (< (str:compare $x $y) 0)))
(= (space-curriculum:install $path)
  (let* (((StoreOpened $owner) (store:open $path "create"))
         ((StoreSnapshot $before $epoch $revision) (store:snapshot $owner))
         ($created (durable-space:create-indexed $owner $before curriculum curriculum-space-v1
           ((SpaceIndexV1 4 0) (SpaceIndexV1 4 2) (SpaceIndexV1 5 0) (SpaceIndexV1 5 2))))
         ($_ (store:close $before)))
    (case $created
      (((KnowledgeCommittedV1 $answer $same $next) (space-curriculum:seed $owner))
       ((KnowledgeReplayedV1 $answer) (space-curriculum:seed $owner))
       ($failure (let $_ (store:close $owner) $failure))))))
(= (space-curriculum:seed $owner)
  (let* (((StoreSnapshot $view $epoch $revision) (store:snapshot $owner))
         ($edits (space-curriculum:edits))
         ($answer (durable-space:commit $owner $view curriculum curriculum-data-v1 $edits))
         ($_ (store:close $view)) ($_ (store:close $owner))) $answer))

; source1 ---- origin1 ---- source2(copy)
;    |                       |
; evidence1,evidence4     evidence2     source3 ---- origin2
;    |                       |             |
; judgement1             judgement2     judgement3
;          \_________________|_____________/
;                            claim1
(= (space-curriculum:supports)
  (KqBind
    (KqSpace curriculum (judgement $judgement claim1 $stance $assessment)
      (Judgement $judgement $stance $assessment))
    (SpaceAnswerV1 (Judgement $judgement $stance $assessment) ($judgement-ref))
    (KqBind
      (KqSpace curriculum (evidence $evidence $judgement $source) (Evidence $evidence $source))
      (SpaceAnswerV1 (Evidence $evidence $source) ($evidence-ref))
      (KqSpace curriculum (source $source $origin $label)
        (Support $judgement $evidence $source $origin $stance $assessment ($judgement-ref $evidence-ref))))))
(= (space-curriculum:optional)
  (KqOptional
    (KqSpace curriculum (judgement $judgement $claim $stance $assessment) (Judgement $judgement $claim))
    (SpaceAnswerV1 (Judgement $judgement $claim) ($judgement-ref))
    (KqSpace curriculum (evidence $evidence $judgement $source)
      (EvidencePresentV1 $judgement-ref $judgement $claim $evidence $source))
    (EvidenceAbsentV1 $judgement-ref $judgement $claim)))

; emit ==(two occurrences)==> parse ---> lex
;   \                         ^ |
;    ---> check --------------+ |
;            ^__________________|
(= (space-curriculum:two-hops)
  (KqBind
    (KqSpace curriculum (dependency $first emit $middle) (First $first $middle))
    (SpaceAnswerV1 (First $first $middle) ($first-ref))
    (KqSpace curriculum (dependency $second $middle $target)
      (TwoHops $first $second emit $middle $target $first-ref))))
(= (space-curriculum:origin
    (SpaceAnswerV1 (Support $judgement $evidence $source $origin $stance $assessment $references) $source-ref))
  (quote $origin))
(= (space-curriculum:count $total $row) (+ $total 1))
(= (space-curriculum:origin-groups)
  (KqGroup (space-curriculum:supports) space-curriculum:origin kb:alpha-equal
    0 space-curriculum:count 65536))
(= (space-curriculum:independent-origins)
  (KqFold
    (KqDistinct (KqMap (space-curriculum:supports) space-curriculum:origin) kb:alpha-equal 65536)
    0 space-curriculum:count 65536))
; This is a provenance/dependence lesson. It does not turn source counts or
; illustrative assessments into independent evidence weights or truth values.
(= (space-curriculum:plans)
  (CurriculumPlansV1 (space-curriculum:supports) (space-curriculum:optional)
    (space-curriculum:two-hops) (space-curriculum:origin-groups)
    (space-curriculum:independent-origins)))
(= (space-curriculum:configuration)
  (let (ValueServiceV1 $socket $data $routes $limits) (query-server:configuration)
    (ValueServiceV1 $socket $data
      (list:append $routes
        ((ServiceRouteV1 GraphOpen Read graph-server:open)
         (ServiceRouteV1 GraphPage Read graph-server:page)
         (ServiceRouteV1 GraphInspect Read graph-server:inspect)
         (ServiceRouteV1 GraphCancel Read graph-server:cancel))) $limits)))
(= (space-curriculum:run (Install $path)) (space-curriculum:install $path))
(= (space-curriculum:run Plans) (space-curriculum:plans))
(= (space-curriculum:run (Query $path $plan $count $fuel))
  (let* (((StoreOpened $owner) (store:open $path "existing"))
         ((StoreSnapshot $view $epoch $revision) (store:snapshot $owner))
         ($cursor (kbq:open-space $view $plan 131072))
         ($_ (store:close $view)) ($_ (store:close $owner)))
    (if (kbq:cursor? $cursor) (space-curriculum:collect $cursor $count $fuel 10000 ()) $cursor)))
(= (space-curriculum:collect $cursor $count $fuel $remaining $pages)
  (if (== $remaining 0) (let $_ (kbq:close $cursor) CurriculumUnfinishedV1)
    (case (kbq:page $cursor $count $fuel)
      (((KqPage $rows (KqMore $next))
        (space-curriculum:collect $next $count $fuel (- $remaining 1) (cons-atom $rows $pages)))
       ((KqPage $rows (KqPaused $next))
        (space-curriculum:collect $next $count $fuel (- $remaining 1) (cons-atom $rows $pages)))
       ((KqPage $rows (KqComplete $work)) (KqPages (list:reverse (cons-atom $rows $pages)) $work))
       ((KqPage $rows (KqFailure $failure $work)) (CurriculumFailedV1 $failure $work))
       ($failure (let $_ (kbq:close $cursor) $failure))))))
