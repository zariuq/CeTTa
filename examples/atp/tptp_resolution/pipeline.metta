; Public lib_tptp reader to the existing first-order resolution example.
;
; Parsing and the compact-record projection remain responsibilities of
; lib_tptp.  This adapter projects admitted clause data into the independently
; admitted resolution-input carrier, then invokes the existing untrusted
; search producer or the independent trace checker.

!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../langdef/logic/clause_data_to_resolution_input_v1.generated.metta)
!(import! &self ./unification.metta)
!(import! &self ./search.metta)
!(import! &self ./trace_check.metta)

(= (tptp-resolution-example:clause-data->input $stage)
   (case $stage
     (((TPTP:StageValue clause-data $problem)
        (tptp:admit-stage
          resolution-input
          "langdef/logic/first_order_resolution_input_v1.metta"
          "Problem"
          (fo-resolution:project-problem $problem)))
      ($outcome $outcome))))

(= (tptp-resolution-example:text->input $text)
   (tptp-resolution-example:clause-data->input
     (tptp:read-text-clause-data $text)))

(= (tptp-resolution-example:file->input $path)
   (tptp-resolution-example:clause-data->input
     (tptp:read-file-clause-data $path)))

(= (tptp-resolution-example:clauses-count
      (fo-resolution:clauses-nil))
   0)

(= (tptp-resolution-example:clauses-count
      (fo-resolution:clauses-cons $_clause $rest))
   (+ 1 (tptp-resolution-example:clauses-count $rest)))

(= (tptp-resolution-example:problem-clause-count
      (fo-resolution:problem $_source $clauses))
   (tptp-resolution-example:clauses-count $clauses))

(= (tptp-resolution-example:search $problem $bounds)
   (case $problem
     (((fo-resolution:problem $_source $_clauses)
        (TPTPResolutionExample.SearchResult
          (tptp-resolution-example:search:prove $problem $bounds)))
      ($outcome
        (TPTPResolutionExample.SearchInputFault $outcome)))))

(= (tptp-resolution-example:check-trace $problem $trace)
   (case
     (tptp-resolution-example:check:certificate $problem $trace)
     (((FOCheck.Verified)
        (TPTPResolutionExample.TraceAccepted))
      ((FOCheck.Rejected $reason)
        (TPTPResolutionExample.TraceRejected $reason))
      ($outcome
        (TPTPResolutionExample.TraceCheckFault $outcome)))))
