; Explicit synthetic count calibration over the existing claims Space.
; One origin/claim is one evidence unit in this lesson. Independence between
; those units is an input assumption, not inferred from names or LLM scores.
; Interpret these counts as two stipulated independent batches of trials;
; distinct source labels alone do not establish that statistical model.
!(import! &self "space_curriculum.metta")
!(import! &self (library lib_wmpln_evidence))

(QueryPureCallbackV1 space-evidence:less)
(QueryPureCallbackV1 space-evidence:key)
(QueryPureCallbackV1 space-evidence:unit-step)
(QueryPureCallbackV1 space-evidence:total-step)
(QueryPureCallbackV1 space-evidence:result)
(QueryPureCallbackV1 space-evidence:ground-row)

; The existing illustrative Assessment values are not used as count data.
; These are separately declared teaching inputs, not empirical calibration.
(= (space-evidence:install $path)
  (case (space-curriculum:install $path)
    (((KnowledgeCommittedV1 $answer $epoch $revision) (space-evidence:seed $path))
     ((KnowledgeReplayedV1 $answer) (space-evidence:seed $path))
     ($failure $failure))))
(= (space-evidence:seed $path)
  (let* (((StoreOpened $owner) (store:open $path "existing"))
         ((StoreSnapshot $view $epoch $revision) (store:snapshot $owner))
         ($answer (durable-space:commit $owner $view curriculum evidence-calibration-v1
           ((SpaceInsertV1 origin1-counts (calibrated-evidence origin1 claim1 8 2))
            (SpaceInsertV1 origin2-counts (calibrated-evidence origin2 claim1 1 4)))))
         ($_ (store:close $view)) ($_ (store:close $owner))) $answer))

; This application projects actual stored ground records before binding them
; into its schema. Open atoms remain ordinary queryable Space data; they do
; not manufacture calibrated origins or counts for this numerical lesson.
(= (space-evidence:ground-facts)
  (KqSelect (KqSpace curriculum $original $original) space-evidence:ground-row))
(: space-evidence:ground-row (-> Atom %Undefined%))
(= (space-evidence:ground-row (SpaceAnswerV1 $original $references))
  (if (knowledge:ground-identity? $original) RelTrue RelFalse))
; Each row retains judgement,evidence,source and calibration memberships.
; Missing calibration is explicit; no supported row vanishes through a join.
(= (space-evidence:supports)
  (KqBind (space-evidence:ground-facts)
    (SpaceAnswerV1 (judgement $judgement claim1 $stance $assessment) ($judgement-ref))
    (KqBind (space-evidence:ground-facts)
      (SpaceAnswerV1 (evidence $evidence $judgement $source) ($evidence-ref))
      (KqBind (space-evidence:ground-facts)
        (SpaceAnswerV1 (source $source $origin $label) ($source-ref))
        (KqOptional
          (KqRows ((EvidenceLineageV1 $origin $judgement $evidence $source)))
          (EvidenceLineageV1 $origin $judgement $evidence $source)
          (KqBind (space-evidence:ground-facts)
            (SpaceAnswerV1 (calibrated-evidence $origin claim1 $positive $negative) ($calibration-ref))
            (KqRows ((SpaceAnswerV1
              (EvidenceSupportV1 $origin $judgement $evidence $source $positive $negative
                ($judgement-ref $evidence-ref) ($source-ref)) ($calibration-ref)))))
          (EvidenceCalibrationAbsentV1 $origin $judgement $evidence $source
            ($judgement-ref $evidence-ref) ($source-ref)))))))
(: space-evidence:less (-> Atom Atom %Undefined%))
(= (space-evidence:less $left $right)
  (let* (((StoreEncoded $a) (store:encode $left)) ((StoreEncoded $b) (store:encode $right)))
    (< (str:compare $a $b) 0)))
(: space-evidence:key (-> Atom %Undefined%))
(= (space-evidence:key $row)
  (case $row
    (((SpaceAnswerV1 (EvidenceSupportV1 $origin $j $e $s $p $n $refs $sources) $calibrations) (quote $origin))
     ((EvidenceCalibrationAbsentV1 $origin $j $e $s $refs $sources) (quote $origin))
     ($bad EvidenceMalformedOriginV1))))
(: space-evidence:counts? (-> Atom Atom Bool))
(= (space-evidence:counts? $positive $negative)
  (if (and (durable-index:integer? $positive) (durable-index:integer? $negative))
    (and (>= $positive 0) (>= $negative 0)) False))

; Unit state is (accepted-counts-or-absence, ordered provenance, problem).
; Equal copies contribute once. Conflicting copies refuse numerical revision.
(: space-evidence:unit-step (-> Atom Atom %Undefined%))
(= (space-evidence:unit-step $state $row)
  (case $row
    (((SpaceAnswerV1 (EvidenceSupportV1 $o $j $e $s $p $n $refs $sources) $calibrations)
      (if (space-evidence:counts? $p $n)
        (space-evidence:unit-add $state $row (evidence $p $n) NoEvidenceProblemV1)
        (space-evidence:unit-add $state $row NoEvidenceCountsV1 InvalidEvidenceCountsV1)))
     ((EvidenceCalibrationAbsentV1 $o $j $e $s $refs $sources)
      (space-evidence:unit-add $state $row NoEvidenceCountsV1 MissingEvidenceCalibrationV1))
     ($bad (space-evidence:unit-add $state $row NoEvidenceCountsV1 MalformedEvidenceRowV1)))))
(: space-evidence:unit-add (-> Atom Atom Atom Atom %Undefined%))
(= (space-evidence:unit-add (EvidenceUnitV1 $counts $reversed $problem) $row $candidate $new-problem)
  (let* (($next (cons-atom $row $reversed))
         ($reason (if (== $problem NoEvidenceProblemV1) $new-problem $problem)))
    (if (== $reason NoEvidenceProblemV1)
      (if (== $counts NoEvidenceCountsV1)
        (EvidenceUnitV1 $candidate $next NoEvidenceProblemV1)
        (if (== $counts $candidate) (EvidenceUnitV1 $counts $next NoEvidenceProblemV1)
          (EvidenceUnitV1 $counts $next ConflictingEvidenceCalibrationV1)))
      (EvidenceUnitV1 $counts $next $reason))))
(= (space-evidence:units)
  (KqGroup (KqOrder (space-evidence:supports) space-evidence:less 65536)
    space-evidence:key kb:alpha-equal
    (EvidenceUnitV1 NoEvidenceCountsV1 () NoEvidenceProblemV1)
    space-evidence:unit-step 65536))
(: space-evidence:total-step (-> Atom Atom %Undefined%))
(= (space-evidence:total-step
    (EvidenceTotalV1 $counts $units $valid)
    (RelGroup $origin (EvidenceUnitV1 $unit $reversed $problem)))
  (let* (($ordered (list:reverse $reversed))
         ($next (cons-atom (EvidenceOriginV1 $origin $unit $ordered $problem) $units)))
    (if (and (== $problem NoEvidenceProblemV1) (not (== $unit NoEvidenceCountsV1)))
      (let $sum (evidence-revision $counts $unit) (EvidenceTotalV1 $sum $next $valid))
      (EvidenceTotalV1 $counts $next False))))
(: space-evidence:result (-> Atom %Undefined%))
(= (space-evidence:result (EvidenceTotalV1 $counts $reversed $valid))
  (let $units (rel:order space-evidence:less $reversed)
    (if $valid
      (let $view (evidence-toSTV $counts 1)
        (EvidenceRevisionV1 claim1 (EvidenceCalibrationV1 natural-counts independent-origin-claim 1) $counts $view $units))
      (EvidenceRevisionRefusedV1 claim1 $units))))
(= (space-evidence:revision)
  (KqMap
    (KqFold (space-evidence:units) (EvidenceTotalV1 (evidence 0 0) () True)
      space-evidence:total-step 65536)
    space-evidence:result))
(= (space-evidence:plans)
  (EvidencePlansV1 (space-evidence:supports) (space-evidence:units) (space-evidence:revision)))
(= (space-evidence:run (Install $path)) (space-evidence:install $path))
(= (space-evidence:run Plans) (space-evidence:plans))
(= (space-evidence:run (Query $path $plan $count $fuel))
  (space-curriculum:run (Query $path $plan $count $fuel)))
(= (space-evidence:run (Kernel $left $right))
  (let $sum (evidence-revision $left $right)
    (EvidenceKernelV1 $sum (evidence-toSTV $sum 1))))
