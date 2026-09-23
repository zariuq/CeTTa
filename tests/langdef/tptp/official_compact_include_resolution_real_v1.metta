!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (test:expanded-count (tptp-ci-v1:expanded-inputs-nil)) 0)
(= (test:expanded-count
      (tptp-ci-v1:expanded-inputs-cons $_head $tail))
   (+ 1 (test:expanded-count $tail)))

(= (test:root-count (tptp-ci-v1:expanded-inputs-nil)) 0)
(= (test:root-count
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $_origin
          (TPTP:IncludePathRootV1 $_root) $_namespace $_name $_record)
        $tail))
   (+ 1 (test:root-count $tail)))
(= (test:root-count
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $_origin
          (TPTP:IncludePathStepV1 $_path $_include $_target)
          $_namespace $_name $_record)
        $tail))
   (test:root-count $tail))

(= (test:included-names (tptp-ci-v1:expanded-inputs-nil)) LNil)
(= (test:included-names
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $_origin
          (TPTP:IncludePathRootV1 $_root) $_namespace $_name $_record)
        $tail))
   (test:included-names $tail))
(= (test:included-names
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $_origin
          (TPTP:IncludePathStepV1 $_path $_include $_target)
          $_namespace (TPTP:QualifiedFormulaNameV1 $_qualified $name) $_record)
        $tail))
   (LCons $name (test:included-names $tail)))

(= (test:family-counts $inputs)
   (test:family-counts-acc $inputs 0 0 0 0 0 0))
(= (test:family-counts-acc
      (tptp-ci-v1:expanded-inputs-nil)
      $fof $cnf $tff $thf $tcf $tpi)
   (TPTP:FamilyCountsV1 $fof $cnf $tff $thf $tcf $tpi))
(= (test:family-counts-acc
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $_origin $_path $_namespace $_name $record)
        $tail)
      $fof $cnf $tff $thf $tcf $tpi)
   (case (tptp:record-family $record)
     ((fof (test:family-counts-acc
             $tail (+ $fof 1) $cnf $tff $thf $tcf $tpi))
      (cnf (test:family-counts-acc
             $tail $fof (+ $cnf 1) $tff $thf $tcf $tpi))
      (tff (test:family-counts-acc
             $tail $fof $cnf (+ $tff 1) $thf $tcf $tpi))
      (thf (test:family-counts-acc
             $tail $fof $cnf $tff (+ $thf 1) $tcf $tpi))
      (tcf (test:family-counts-acc
             $tail $fof $cnf $tff $thf (+ $tcf 1) $tpi))
      (tpi (test:family-counts-acc
             $tail $fof $cnf $tff $thf $tcf (+ $tpi 1))))))

(= (test:edge-count (tptp-ci-v1:include-edges-nil)) 0)
(= (test:edge-count
      (tptp-ci-v1:include-edges-cons $_head $tail))
   (+ 1 (test:edge-count $tail)))

(= (test:real-summary $outcome)
   (case $outcome
     (((TPTP:ExpandedDocumentV1 $_root $inputs $edges)
        (TPTP:RealIncludeSummaryV1
          (test:expanded-count $inputs)
          (test:root-count $inputs)
          (test:included-names $inputs)
          (test:family-counts $inputs)
          (test:edge-count $edges)))
      ($fault $fault))))

(= (test:run $puz $hwc $selected $library)
   (TptpOfficialCompactIncludeResolutionRealV1
     (test:real-summary
       (tptp:read-file-expanded $puz (TPTP:LibraryRoot $library)))
     (test:real-summary
       (tptp:read-file-expanded $hwc (TPTP:LibraryRoot $library)))
     (test:real-summary
       (tptp:read-file-expanded $selected (TPTP:LibraryRoot $library)))))

!(println!
   (test:run
     (system:arg 0) (system:arg 1) (system:arg 2) (system:arg 3)))
