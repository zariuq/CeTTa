; Public lib_tptp reader to the first-order resolution example.
;
; Parsing and the compact form remain responsibilities of lib_tptp, which
; gives the clauses of a document as the admitted resolution-input carrier:
; the clauses of one file, of a file with its includes expanded, or of files
; whose formulas were clausified.  This adapter invokes the untrusted search
; producer, the independent trace checker, or the provenance check that
; relates the clauses to their source documents.

!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ./unification.metta)
!(import! &self ./search.metta)
!(import! &self ./trace_check.metta)
!(import! &self ./provenance_check.metta)

(= (tptp-resolution-example:text->input $text)
   (tptp:read-text-resolution-input $text))

(= (tptp-resolution-example:file->input $path)
   (tptp:read-file-resolution-input $path))

(= (tptp-resolution-example:file->expanded-input $path $library-root)
   (tptp:read-file-expanded-resolution-input $path $library-root))

(= (tptp-resolution-example:file->clausified-input $path $library-root)
   (tptp:read-file-clausified-resolution-input $path $library-root))

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

(= (tptp-resolution-example:check-provenance $environment $problem $clausifications)
   (case
     (tptp-resolution-example:provenance:check $environment $problem $clausifications)
     (((FOProvenance.Verified)
        (TPTPResolutionExample.ProvenanceAccepted))
      ((FOProvenance.Rejected $reason)
        (TPTPResolutionExample.ProvenanceRejected $reason))
      ($outcome
        (TPTPResolutionExample.ProvenanceCheckFault $outcome)))))
