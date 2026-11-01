!(import! &self (library lib_ic_library_cost))
!(import! &self (library lib_ic_source_seeds))
!(import! &self (library str))

(= (cost-test:check $name $value) (println! (CostCheck $name (== $value True))))
(= (cost-test:accepted? $result)
   (case $result (((IC:PackedAccepted $value $before $after) (< $after $before)) ($other False))))
(= (cost-test:source)
   (syntax:read "(= (a $x) (+ (* (+ $x 1) (+ $x 1)) (* (+ $x 1) (+ $x 1))))
(= (b $y) (+ (* (+ $y 1) (+ $y 1)) (* (+ $y 1) (+ $y 1))))
(= (c $z) (+ (* (+ $z 1) (+ $z 1)) (* (+ $z 1) (+ $z 1))))
(= (d $w) (+ (* (+ $w 1) (+ $w 1)) (* (+ $w 1) (+ $w 1))))"))
(= (cost-test:long-name)
   (str:concat "ic-learned:"
     (str:join "" (map-atom (0 1 2 3 4 5 6 7) $index
       "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"))))
(= (cost-test:changed-policy (IC:PreparedSearch $state))
   (case $state
     (((IC:SearchState (IC:Task (Library:CostModel Nodes $model) $original $observation $guards $quantum)
         $queue $best $statistics $fault)
       (let* (($changed (IC:SearchState
                 (IC:Task (Library:CostModel Bytes $model) $original $observation $guards $quantum)
                 $queue $best $statistics $fault))
              ($result (ic:resume $changed 0)) ($released (ic:release $state)))
         (== $result (IC:ResumeRejected InvalidArtifact))))
      ($other False))))
(= (cost-test:candidates $prepared $original $pair)
   (case $pair
     ((($left $right)
       (case ((context-library:seed $prepared "ic-learned:a" $left $right)
              (context-library:seed $prepared (cost-test:long-name) $left $right))
         ((((Library:Candidate $short $short-support) (Library:Candidate $long $long-support))
           (let* (($a (cost-test:check RenamingPreservesNodes
                        (== (ic:library-cost Nodes $short) (ic:library-cost Nodes $long))))
                  ($b (cost-test:check RenamingChangesBytes
                        (not (== (ic:library-cost Bytes $short) (ic:library-cost Bytes $long)))))
                  ($c (cost-test:check NodesAcceptLargerBytes
                        (cost-test:accepted? (ic:library-check Nodes $original $original $long))))
                  ($d (cost-test:check BytesRefuseLargerBytes
                        (== (ic:library-check Bytes $original $original $long) (IC:PackedRejected NotSmaller))))
                  ($e (cost-test:check BytesAcceptMoreNodes
                        (cost-test:accepted? (ic:library-check Bytes $original $long $original))))
                  ($f (cost-test:check NodesRefuseMoreNodes
                        (== (ic:library-check Nodes $original $long $original) (IC:PackedRejected NotSmaller))))
                  ($g (cost-test:check EqualNodesRefused
                        (== (ic:library-check Nodes $original $short $short) (IC:PackedRejected NotSmaller))))
                  ($h (cost-test:check LegacyBytesUnchanged
                        (== (ic:library-check $original $original $short)
                            (ic:library-check Bytes $original $original $short))))
                  ($i (cost-test:check UnknownCostRefused
                        (== (ic:library-check UnknownCost $original $original $short)
                            (IC:PackedRejected InvalidCost))))
                  ($j (cost-test:check MalformedRefused
                        (== (ic:library-check Nodes $original $original (tensor:from-values u8 (1) (0)))
                            (IC:PackedRejected InvalidArtifact))))
                  ($k (cost-test:check WrongOriginalRefused
                        (== (ic:library-check Nodes (ic:source-pack (syntax:read "(different)")) $original $short)
                            (IC:PackedRejected Reconstruction))))
                  ($l (cost-test:check ChangedPolicyRefused
                        (cost-test:changed-policy
                          (ic:continue-library Nodes Deferred $original "policy" 128 (Library:Seeds ($pair)))))))
             (println! (CostChecksFinished))))
          ($other (println! (CostCheck CandidateGeneration False)))))))))

!(cost-test:check EmptyExpression (== (library-cost:term (Syntax:Expr ())) 1))
!(cost-test:check Variable (== (library-cost:term (Syntax:Variable 0)) 1))
!(cost-test:check WholeEquation (== (library-cost:document (syntax:read "(= (f $x) (== $x ()))")) 9))
!(cost-test:check ThunkCharged (== (library-cost:document (syntax:read "(|-> () $x)")) 4))
!(cost-test:check LiteralSpelling (== (library-cost:document (syntax:read "(a 1 \"short\")"))
                                     (library-cost:document (syntax:read "(long-name 999999 \"long string\")"))))
!(let* (($source (cost-test:source)) ($original (ic:source-pack $source))
        ($prepared (ic:prepare-library Deferred $source)))
   (case (ic:source-seed-region $prepared 1)
     (((Library:SeedRegion ($pair) $available) (cost-test:candidates $prepared $original $pair))
      ($other (println! (CostCheck SeedSelection False))))))
