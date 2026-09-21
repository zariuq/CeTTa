!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (test:expanded-names
      (tptp-ci-v1:expanded-inputs-nil))
   LNil)
(= (test:expanded-names
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $_origin $_path $_namespace
          (TPTP:QualifiedFormulaNameV1 $_qualified $name) $_record)
        $tail))
   (LCons $name (test:expanded-names $tail)))

(= (test:expanded-namespaces
      (tptp-ci-v1:expanded-inputs-nil))
   LNil)
(= (test:expanded-namespaces
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $_origin $_path $namespace $_name $_record)
        $tail))
   (LCons $namespace (test:expanded-namespaces $tail)))

(= (test:expanded-summary $outcome)
   (case $outcome
     (((TPTP:ExpandedDocumentV1 $_root $inputs $_edges)
        (TPTP:ExpandedSummaryV1
          (test:expanded-names $inputs)
          (test:expanded-namespaces $inputs)))
      ($fault $fault))))

(= (test:documents-count (tptp-ci-v1:documents-nil)) 0)
(= (test:documents-count
      (tptp-ci-v1:documents-cons $_head $tail))
   (+ 1 (test:documents-count $tail)))
(= (test:bindings-count (tptp-ci-v1:bindings-nil)) 0)
(= (test:bindings-count
      (tptp-ci-v1:bindings-cons $_head $tail))
   (+ 1 (test:bindings-count $tail)))
(= (test:environment-counts $outcome)
   (case $outcome
     (((TPTP:CompactIncludeEnvironmentV1 $_root $_library
          (tptp-ci-v1:environment $documents $bindings))
        (TPTP:EnvironmentCountsV1
          (test:documents-count $documents)
          (test:bindings-count $bindings)))
      ($fault $fault))))

(= (test:fault-kind $outcome)
   (case $outcome
     (((TPTP:IncludeResolutionFaultV1
          (TPTP:IncludeCycleV1 $_source $_target $_path)) cycle)
      ((TPTP:IncludeResolutionFaultV1
          (TPTP:IncludeMissingSelectionNameV1 $_target $_name))
        missing-selection)
      ((TPTP:IncludeResolutionFaultV1
          (TPTP:IncludeDuplicateSelectionNameV1 $_target $_name))
        duplicate-selection)
      ((TPTP:IncludeResolutionFaultV1
          (TPTP:IncludeAmbiguousSelectionNameV1 $_target $_name))
        ambiguous-selection)
      ($other $other))))

(= (test:run $root $duplicate $cycle $selection $missing $duplicate-selection
      $ambiguous $library)
   (let $duplicate-environment
        (tptp:load-compact-include-environment
          $duplicate (TPTP:LibraryRoot $library))
     (TptpOfficialCompactIncludeResolutionV1
       (test:expanded-summary
         (tptp:read-file-expanded $root (TPTP:LibraryRoot $library)))
       (test:expanded-summary
         (tptp:resolve-loaded-compact-includes $duplicate-environment))
       (test:environment-counts $duplicate-environment)
       (test:fault-kind
         (tptp:read-file-expanded $cycle (TPTP:LibraryRoot $library)))
       (test:expanded-summary
         (tptp:read-file-expanded $selection
           (TPTP:LibraryRoot $library)))
       (test:fault-kind
         (tptp:read-file-expanded $missing
           (TPTP:LibraryRoot $library)))
       (test:fault-kind
         (tptp:read-file-expanded $duplicate-selection
           (TPTP:LibraryRoot $library)))
       (test:fault-kind
         (tptp:read-file-expanded $ambiguous
           (TPTP:LibraryRoot $library))))))

!(println!
   (test:run
     (system:arg 0) (system:arg 1) (system:arg 2) (system:arg 3)
     (system:arg 4) (system:arg 5) (system:arg 6) (system:arg 7)))
