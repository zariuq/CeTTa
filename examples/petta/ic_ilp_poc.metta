; Learn a helper from the numerical observation codec, then use its unchanged
; definition as vocabulary for a finite typed ILP composition experiment.
; Run from the repository root: cetta --lang petta examples/petta/ic_ilp_poc.metta
!(import! &self (library lib_ic_source_seeds))
!(import! &self (library lib_ic_mil_source))
!(import! &self (library fs))

(= (poc:bytes $artifact) (car-atom (tensor:shape $artifact)))

(= (poc:finish (IC:SearchResult $best $statistics $reason $fault $state))
   (let $released (ic:release $state)
     (Poc:Search $best $statistics $reason $fault)))

(= (poc:resume (IC:PreparedSearch $state) $split)
   (if $split
     (case (ic:resume $state 1)
       (((IC:SearchResult $best $statistics BudgetExhausted $fault $next)
         (poc:finish (ic:resume $next 127)))
        ($finished (poc:finish $finished))))
     (poc:finish (ic:resume $state 128))))

(= (poc:search $artifact $suffix $split)
   (case (library-stack:view $artifact)
     (((Library:StackValue $layers $document $original)
       (let $prepared (ic:prepare-library Deferred $document)
         (case (ic:source-seed-region $prepared 12)
           (((Library:SeedRegion $pairs $available)
             (poc:resume
               (ic:continue-library Deferred $artifact $suffix 2048 (Library:Seeds $pairs))
               $split))
            ($refused $refused)))))
      ($refused $refused))))

; The classifier is learned. These two small tests are declared background
; vocabulary: the second deliberately also produces an incorrect answer.
(= (poc:extra-source)
   "(= (poc:is-exact $x) (== $x (IC:Some Numeric:Exact)))\n(= (poc:leaky-test $x) (superpose ((== $x $x) False)))")
(= (poc:family)
   (MIL.SourceFamily
     ((MIL.SourceConstructor "IC:Some" 1)
      (MIL.SourceConstructor "IC:None" 0)
      (MIL.SourceConstructor "Numeric:Exact" 0))
     ((MIL.SourceBinding "ic-learned:kind" (MIL.Rel TensorKind Criterion))
      (MIL.SourceBinding "poc:is-exact" (MIL.Rel Criterion Bool))
      (MIL.SourceBinding "poc:leaky-test" (MIL.Rel Criterion Bool)))))
(= (poc:skeleton)
   (MIL.HO1 MIL.MapOp (MIL.Chain (MIL.Hole classify) (MIL.Hole test))))
(= (poc:type) (MIL.Rel (MIL.List TensorKind) (MIL.List Bool)))
(= (poc:examples $negative)
   (MIL.Examples
     ((MIL.Ex ("BoolTensor" "I64Tensor") (True True)))
     $negative MIL.Relational))
(= (poc:learn $types $semantics $type $examples)
   (collapse
     (let $program (mil:prime:typed-fill-vocabulary $types (poc:skeleton) $type)
       (if (mil:consistent $semantics $program $examples) $program (superpose ())))))
(= (poc:ilp $background)
   (case (ic:prepare-background Source:MIL $background ())
     (((MIL.SourcePrepared $family $document $types $semantics)
       (let* (($positive (poc:learn $types $semantics (poc:type) (poc:examples ())))
              ($selected (poc:learn $types $semantics (poc:type)
                (poc:examples ((MIL.Ex ("BoolTensor") (False))))))
              ($contradiction (poc:learn $types $semantics (poc:type)
                (poc:examples ((MIL.Ex ("BoolTensor" "I64Tensor") (True True))))))
              ($wrong-sort (poc:learn $types $semantics (MIL.Rel Number Number) (poc:examples ())))
              ($predictions
                (map-atom $selected $program
                  (map-atom (("F64Tensor" "Unknown" "BoolTensor" "I64Tensor" "BoolTensor")
                             () ("I64Tensor" "F64Tensor")) $input
                    (Poc:Prediction $input (mil:outputs $semantics $program $input))))))
         (println! (Poc:ILP $positive $selected $contradiction $wrong-sort $predictions
           (tensor:inspect $background)))))
      ($refused (println! (Poc:Fault $refused))))))

(= (poc:background $artifact)
   (case (ic:source-background $artifact
           (MIL.SourceFamily
             ((MIL.SourceConstructor "IC:Some" 1) (MIL.SourceConstructor "IC:None" 0)
              (MIL.SourceConstructor "Numeric:Exact" 0))
             ((MIL.SourceBinding "ic-learned:kind" (MIL.Rel TensorKind Criterion)))))
     (((MIL.SourceBytes $bytes)
       (case ((mil-source:unpack $bytes) (syntax:read (poc:extra-source)))
         ((((MIL.SourceBundle $family (Syntax:Document $version $reader $forms))
             (Syntax:Document $other-version $other-reader $extra))
           (case (mil-source:pack (poc:family)
                   (Syntax:Document $version $reader (union-atom $forms $extra)))
             (((Binary:TreeBytes $background) (poc:ilp $background))
              ($refused (println! (Poc:Fault $refused))))))
          ($refused (println! (Poc:Fault $refused))))))
      ($refused (println! (Poc:Fault $refused))))))

(= (poc:run $source)
   (let* (($original (ic:source-pack $source))
          ($continuous (poc:search $original "kind" False))
          ($resumed (poc:search $original "kind" True)))
     (case $continuous
       (((Poc:Search $best $statistics $reason $fault)
         (case (library-stack:view $best)
           (((Library:StackValue $layers $document $restored)
             (let* (($printed (println! (Poc:Compression (poc:bytes $original) (poc:bytes $best)
                       $statistics $reason $fault (== $continuous $resumed) (== $source $restored)
                       $layers (tensor:inspect $best)
                       (tensor:inspect (binary:utf8 (syntax:write $document))))))
                    ($next (poc:search $best "next" False))
                    ($continuation (case $next
                      (((Poc:Search $after $next-statistics $next-reason $next-fault)
                        (println! (Poc:Next (poc:bytes $best) (poc:bytes $after)
                          $next-statistics $next-reason $next-fault
                          (== (ic:library-unpack $after) (IC:Some $source)))))
                       ($refused (println! (Poc:Fault $refused)))))))
               (poc:background $best)))
            ($refused (println! (Poc:Fault $refused))))))
        ($refused (println! (Poc:Fault $refused)))))))

!(poc:run (syntax:read (fs:read-text "lib/petta/lib_ic_numeric_arrays.metta")))
