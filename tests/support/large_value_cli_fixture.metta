!(import! &self "large_value_space_fixture.metta")
!(import! &self (library system))
!(import! &self (library str))
(= (large-cli:argument $value)
  (case (get-metatype $value) ((Symbol (str:symbol->string $value)) ($other $value))))
(: large-cli:grow (-> String Number String))
(= (large-cli:grow $text $steps)
  (if (== $steps 0) $text (large-cli:grow (str:concat $text $text) (- $steps 1))))
(= (large-cli:run $path $mode)
  (case $mode
    (("seed"
      (let* (($created (large-value:run (Seed (Create $path ((SpaceIndexV1 4 0))))))
             ($text (large-cli:grow "x" 21)) ($fact (metric "same" 7 $text))
             ($edits ((SpaceInsertV1 "one" $fact)))
             ($answer (large-value:run (Seed (At $path (Commit "large-cli-seed" $edits))))))
        (case $answer
          (((KnowledgeCommittedV1 $v $e $r) LargeCliCommitted)
           ($failure (Error large-cli-seed $failure))))))
     ("verify"
      (let* (((MemoryRows ((Metric $kind $score $text)))
               (large-value:run (Raw $path (metric $k $n $v) (Metric $k $n $v))))
             ($expected (large-cli:grow "x" 21)) ($bytes (str:byte-length $text)))
        (if (and (== $bytes 2097152) (== $text $expected)) LargeCliReopened
          (Error large-cli-content))))
     ($bad (Error large-cli-mode)))))
!(large-cli:run (large-cli:argument (system:arg 0)) (large-cli:argument (system:arg 1)))
