; Structural source size counts each atom and each expression, including ().
; The stored rewritten program includes every helper definition, parameter,
; call and delayed argument. Spelling, source layout and codec metadata are
; outside this score. Bytes remains the complete serialized-artifact measure.
!(import! &self (library lib_ic_library_stack))

(= (library-cost:term $term)
   (case $term
     (((Syntax:Variable $index) 1)
      ((Syntax:Literal $kind $bytes) 1)
      ((Syntax:Expr $children)
       (+ 1 (foldl-atom $children 0 $total $child (+ $total (library-cost:term $child)))))
      ($invalid IC:None))))

(= (ic-packed:cost Nodes IC:LibraryArchive $artifact)
   (ic:library-cost Nodes $artifact))
(= (ic:library-check $policy $original $current $candidate)
   (ic-packed:check $policy IC:LibraryArchive $original $current $candidate))

; The cost policy is part of the prepared task observation. Changing it in a
; retained state cannot reuse the old observation on resumption.
(= (ic-search:observation (Library:CostModel $policy $model) $artifact)
   (if (== $policy Nodes)
    (case ((ic-search:observation $model $artifact) (ic:library-cost Nodes $artifact))
     ((((IC:Some $observation) (IC:Some $cost))
       (IC:Some (Library:CostObservation Nodes $observation)))
      ($invalid IC:None))) IC:None))
(= (ic-search:materialize (Library:CostModel Nodes $model) $original $proposal)
   (ic-search:materialize $model $original $proposal))
(= (ic-search:consider (Library:CostModel Nodes $model) $original $best $candidate)
   (case (ic:library-check Nodes $original $best $candidate)
     (((IC:PackedAccepted $next $before $after) (IC:CandidateAccepted $next))
      ((IC:PackedRejected $reason) (IC:CandidateRejected $reason)))))
(= (library-cost:wrap $state)
   (case $state
     (((IC:SearchState (IC:Task $model $original $observation $guards $quantum)
         $queue $best $statistics $fault)
       (case (ic:library-cost Nodes $original)
         (((IC:Some $cost)
           (IC:PreparedSearch
             (IC:SearchState
               (IC:Task (Library:CostModel Nodes $model) $original
                 (Library:CostObservation Nodes $observation) $guards $quantum)
               $queue $best $statistics $fault)))
          ($invalid (let $released (ic:release $state) (IC:SearchRefused InvalidCost))))))
      ($invalid (IC:SearchRefused InvalidState)))))
(= (ic:continue-library $policy $mode $artifact $suffix $quantum $region)
   (case $policy
     ((Bytes (ic:continue-library $mode $artifact $suffix $quantum $region))
      (Nodes
        (case (ic:continue-library $mode $artifact $suffix $quantum $region)
          (((IC:PreparedSearch $state) (library-cost:wrap $state)) ($refused $refused))))
      ($invalid (IC:SearchRefused InvalidCost)))))
(= (library-cost:document (Syntax:Document $version $reader $forms))
   (foldl-atom $forms 0 $total $form
     (case $form
       (((Syntax:Form $names $term) (+ $total (library-cost:term $term)))
        ($invalid IC:None)))))
(= (ic:library-cost Bytes $artifact)
   (case (library-stack:view $artifact)
     (((Library:StackValue $layers $document $original)
       (IC:Some (car-atom (tensor:shape $artifact))))
      ($invalid IC:None))))
(= (ic:library-cost Nodes $artifact)
   (case (binary:unpack-fields $artifact)
     (((Binary:Fields ($version $codec $payload))
       (if (binary-schema:member? (binary:from-utf8 $codec) ("ST1" "LC1" "LC2" "LM1"))
         (case (library-stack:view $artifact)
           (((Library:StackValue $layers $document $original)
             (let $cost (library-cost:document $document)
               (if (binary:unsigned? $cost) (IC:Some $cost) IC:None)))
            ($invalid IC:None))) IC:None))
      ($invalid IC:None))))
