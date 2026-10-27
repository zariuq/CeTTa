; Finite typed first-order template generation, independent of pairwise seeds.
; Depth bounds constructor nesting. Hole identities are their first positions
; in the hole-occurrence stream, shared across a forest and both rule sides.
!(import! &self "lib/petta/lib_ic_search.metta")

(= (ic-grammar:choose $values)
   (if (== $values ()) (superpose ()) (car-atom $values)))
(= (ic-grammar:choose $values)
   (if (== $values ()) (superpose ()) (ic-grammar:choose (cdr-atom $values))))

(= (ic-grammar:operators $signature $index)
   (if (== $signature ()) (superpose ())
     (IC:IndexedOperator $index (car-atom $signature))))
(= (ic-grammar:operators $signature $index)
   (if (== $signature ()) (superpose ())
     (ic-grammar:operators (cdr-atom $signature) (+ $index 1))))

; New and repeated parameters are separate alternatives. A reuse increments
; occurrence position even though it does not allocate a new parameter.
(= (ic-grammar:term $signature $role $sort $depth (IC:HoleState $position $holes))
   (if (< $position 9223372036854775807)
     (let $next (cons (IC:Binding $role $position $sort) $holes)
       (IC:Expansion (IC:Hole $role $position) (IC:HoleState (+ $position 1) $next)))
     (IC:GrammarFault Capacity)))
(= (ic-grammar:term $signature $role $sort $depth (IC:HoleState $position $holes))
   (if (< $position 9223372036854775807)
     (case (ic-grammar:choose $holes)
       (((IC:Binding $old-role $index $old-sort)
         (if (and (== $role $old-role) (== $sort $old-sort))
           (IC:Expansion (IC:Hole $role $index) (IC:HoleState (+ $position 1) $holes))
           (superpose ())))))
     (superpose ())))
(= (ic-grammar:term $signature $role $sort $depth $state)
   (if (> $depth 0)
     (case (ic-grammar:operators $signature 0)
       (((IC:IndexedOperator $index (IC:Operator $actual $slots))
         (if (== $actual $sort)
           (case (ic-grammar:many $signature $slots (- $depth 1) $state)
             (((IC:Expansion $children $next) (IC:Expansion (IC:Node $index $children) $next))
              ((IC:GrammarFault $reason) (IC:GrammarFault $reason))))
           (superpose ())))))
     (superpose ())))

(= (ic-grammar:many $signature $slots $depth $state)
   (if (== $slots ()) (IC:Expansion () $state)
     (let* (($slot (car-atom $slots)) ($tail (cdr-atom $slots)))
       (case $slot
         (((IC:Slot $role $sort)
           (case (ic-grammar:term $signature $role $sort $depth $state)
             (((IC:Expansion $pattern $middle)
               (case (ic-grammar:many $signature $tail $depth $middle)
                 (((IC:Expansion $patterns $next)
                   (let $forest (cons $pattern $patterns) (IC:Expansion $forest $next)))
                  ((IC:GrammarFault $reason) (IC:GrammarFault $reason)))))
              ((IC:GrammarFault $reason) (IC:GrammarFault $reason))))))))))

(= (ic-grammar:enumerate $signature $slots $depth)
   (if (and (ic-template:signature? $signature)
            (and (ic-template:slots? $slots) (binary:unsigned? $depth)))
     (case (ic-grammar:many $signature $slots $depth (IC:HoleState 0 ()))
       (((IC:Expansion $patterns $state) (IC:Template $patterns))
        ((IC:GrammarFault $reason) (IC:GrammarFault $reason))))
     (IC:GrammarFault InvalidGrammar)))

; Only structural generation runs inside the retained producer. Archive
; materialization and complete-byte acceptance belong to the task model.
; This worker receives an admitted finite signature and depth from preparation.
(= (ic-grammar:produce $signature $name $slots $depth)
   (case (ic-grammar:many $signature $slots $depth (IC:HoleState 0 ()))
     (((IC:Expansion $patterns $state) (IC:Proposal (IC:Factor $name $slots $patterns)))
      ((IC:GrammarFault $reason) (IC:ProducerFault $reason)))))

; Preparation validates the task first; opening the producer does not start
; search. The ordinary resumable controller owns execution and acceptance.
(= (ic:learn-programs $archive $name $slots $depth $quantum)
   (case (ic-archive:prepare $archive)
     (((IC:PreparedArchive $tree (IC:Catalog $version $observation $signature $heads $literals) $source)
       (if (and (binary-schema:text? $name)
                (and (ic-template:slots? $slots) (binary:unsigned? $depth)))
         (let* (($space (context-space))
                ($handle (work-new (ic-grammar:produce $signature $name $slots $depth))))
           (case (ic:prepare IC:ProgramArchive $archive ($space) $quantum ((IC:Branch 1 $handle)))
             (((IC:PreparedSearch $state) (IC:PreparedSearch $state))
              ($other (let $released (work-cancel $handle) $other)))))
         (IC:SearchRefused InvalidGrammar)))
      ($other (IC:SearchRefused InvalidArchive)))))
