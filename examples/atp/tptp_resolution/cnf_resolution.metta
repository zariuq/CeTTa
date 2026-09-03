; A resolution-search example that runs under Prime, PeTTa, and HE profiles.
;
; `lib_tptp` only parses and transforms the standard input.  This directory
; supplies the chosen search policy and private trace checker as authored
; MeTTa.  The two operations are invoked separately below; TPTP itself does
; not define a prover and local trace replay is not TSTP verification.

!(import! &self ./pipeline.metta)

(= (example:input-summary $stage)
   (case $stage
     (((TPTP:StageValue resolution-input $problem)
        (TPTPResolutionExample.InputAdmitted
          (tptp-resolution-example:problem-clause-count $problem)))
      ($result (TPTPResolutionExample.InputFailed $result)))))

(= (example:search-summary $result)
   (case $result
     (((TPTPResolutionExample.SearchResult
         (FOSearch.Result
           (FOSearch.Refutation $trace)
           (FOSearch.Stats $given $generated)))
        (TPTPResolutionExample.RefutationFound
          $trace $given $generated))
      ((TPTPResolutionExample.SearchResult
         (FOSearch.Result
           (FOSearch.Incomplete $reason)
           (FOSearch.Stats $given $generated)))
        (TPTPResolutionExample.Incomplete $reason $given $generated))
      ($other (TPTPResolutionExample.SearchFailed $other)))))

(= (example:visible-search-summary $search-result)
   (case $search-result
     (((TPTPResolutionExample.RefutationFound
         $trace $given $generated)
        (TPTPResolutionExample.RefutationFound $given $generated))
      ($other $other))))

(= (example:run-search $stage $bounds)
   (case $stage
     (((TPTP:StageValue resolution-input $problem)
        (let $search
             (example:search-summary
               (tptp-resolution-example:search $problem $bounds))
          (TPTPResolutionExample.SearchRun
            (example:input-summary $stage)
            (example:visible-search-summary $search))))
      ($result
        (TPTPResolutionExample.SearchRun
          (example:input-summary $result)
          (TPTPResolutionExample.SearchFailed $result))))))

(= (example:run-search-and-check $stage $bounds)
   (case $stage
     (((TPTP:StageValue resolution-input $problem)
        (let $search
             (example:search-summary
               (tptp-resolution-example:search $problem $bounds))
          (case $search
            (((TPTPResolutionExample.RefutationFound
                $trace $given $generated)
               (let $checked
                    (tptp-resolution-example:check-trace $problem $trace)
                 (TPTPResolutionExample.SearchAndCheckRun
                   (example:input-summary $stage)
                   (TPTPResolutionExample.RefutationFound
                     $given $generated)
                   (case $checked
                     (((TPTPResolutionExample.TraceAccepted)
                        (TPTPResolutionExample.TraceAccepted
                          $given $generated))
                      ($other
                        (TPTPResolutionExample.TraceNotAccepted
                          $other)))))))
             ($other
               (TPTPResolutionExample.SearchAndCheckRun
                 (example:input-summary $stage)
                 (example:visible-search-summary $other)
                 (TPTPResolutionExample.NoTraceToCheck)))))))
      ($result
        (TPTPResolutionExample.SearchAndCheckRun
          (example:input-summary $result)
          (TPTPResolutionExample.SearchFailed $result)
          (TPTPResolutionExample.NoTraceToCheck))))))

!(example:run-search-and-check
   (tptp-resolution-example:file->input
     "examples/atp/tptp_resolution/nonground_refutation.p")
   (FOSearch.Bounds 20 200))

; The negative control is satisfiable.  Bounded search exhausts its passive
; clauses and reports incompleteness; it never fabricates a trace.
!(example:run-search
   (tptp-resolution-example:file->input
     "examples/atp/tptp_resolution/satisfiable.p")
   (FOSearch.Bounds 20 200))
