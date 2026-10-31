; Synthetic program sizes. Stored numbers are example data, not measurements
; or evidence weights. Range and exact leaves share the same captured owner.
!(import! &self "space_curriculum.metta")

; emit --depends--> parse --depends--> lex
; two equal (program emit 40) payloads have different membership identities.
(= (range-curriculum:install $path)
  (let* (((StoreOpened $owner) (store:open $path "create"))
         ((StoreSnapshot $before $epoch $revision) (store:snapshot $owner))
         ($created (durable-space:create-indexed $owner $before range-lessons range-space-v1
           ((SpaceIndexV1 3 0) (SpaceRangeIndexV1 3 2))))
         ($_ (store:close $before)))
    (case $created
      (((KnowledgeCommittedV1 $answer $same $next) (range-curriculum:seed $owner))
       ((KnowledgeReplayedV1 $answer) (range-curriculum:seed $owner))
       ($failure (let $_ (store:close $owner) $failure))))))
(= (range-curriculum:seed $owner)
  (let* (((StoreSnapshot $view $epoch $revision) (store:snapshot $owner))
         ($answer (durable-space:commit $owner $view range-lessons range-data-v1
           ((SpaceInsertV1 0 (program lex 5))
            (SpaceInsertV1 1 (program parse 20))
            (SpaceInsertV1 2 (program check 10))
            (SpaceInsertV1 3 (program emit 40))
            (SpaceInsertV1 4 (program emit 40))
            (SpaceInsertV1 5 (program compile 20.5))
            (SpaceInsertV1 6 (depends emit parse))
            (SpaceInsertV1 7 (depends parse lex)))))
         ($_ (store:close $view)) ($_ (store:close $owner))) $answer))
(= (range-curriculum:plans)
  ((KqRange range-lessons 3 2 (Inclusive 10) (Inclusive 30)
     (program $name $size) (Program $name $size))
   (KqRange range-lessons 3 2 (Inclusive 40) (Inclusive 40)
     (program $name $size) (Program $name $size))
   (KqBind
     (KqRange range-lessons 3 2 (Inclusive 10) NoBound
       (program $name $size) (Program $name $size))
     (SpaceAnswerV1 (Program $name $size) ($membership))
     (KqSpace range-lessons (depends $name $dependency)
       (Dependency $name $dependency $membership)))))
(= (range-curriculum:run $path)
  (case (range-curriculum:install $path)
    (((KnowledgeCommittedV1 $answer $epoch $revision) (range-curriculum:lessons $path))
     ((KnowledgeReplayedV1 $answer) (range-curriculum:lessons $path))
     ($failure $failure))))
(= (range-curriculum:lessons $path)
  (let $answers
    (map-atom (range-curriculum:plans) $plan (space-curriculum:run (Query $path $plan 1 3)))
    (RangeCurriculumV1 $answers)))
