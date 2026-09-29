; Nested file imports are resolved in the top-level script context.  Load the
; example's authored search dependencies explicitly so this gate remains
; independent of its location in the tree.
!(import! &self ../../../examples/atp/tptp_resolution/unification.metta)
!(import! &self ../../../examples/atp/tptp_resolution/search.metta)
!(import! &self ../../../examples/atp/tptp_resolution/trace_check.metta)
!(import! &self ../../../examples/atp/tptp_resolution/pipeline.metta)
!(import! &self system)

(= (gate:search-summary $problem $result)
   (case $result
     (((FOSearch.Result
         (FOSearch.Refutation $trace)
         (FOSearch.Stats $given $generated))
        (case (tptp-resolution-example:check-trace $problem $trace)
          (((TPTPResolutionExample.TraceAccepted)
             (TptpOfficialCompactResolutionConsumerV1
               (tptp-resolution-example:problem-clause-count $problem)
               Refutation $given $generated TraceAccepted))
           ($other
             (TptpOfficialCompactResolutionTraceFailureV1 $other)))))
      ((FOSearch.Result
         (FOSearch.Incomplete $reason)
         (FOSearch.Stats $given $generated))
        (TptpOfficialCompactResolutionConsumerV1
          (tptp-resolution-example:problem-clause-count $problem)
          (Incomplete $reason) $given $generated NoTrace))
      ($other
        (TptpOfficialCompactResolutionSearchFailureV1 $other)))))

(= (gate:summary $stage)
   (case $stage
     (((TPTP:StageValue resolution-input $problem)
        (gate:search-summary
          $problem
          (tptp-resolution-example:search:prove
            $problem (FOSearch.Bounds 20 200))))
      ($outcome
        (TptpOfficialCompactResolutionConsumerFailureV1 $outcome)))))

!(println!
   (gate:summary
     (tptp-resolution-example:file->input (system:arg 0))))
