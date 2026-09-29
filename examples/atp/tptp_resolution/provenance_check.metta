; Example: source and include provenance of a resolution problem.
;
; A problem read from a file with its includes expanded states, of each input
; clause, the file it comes from (that file's digest), its position in that
; file, and the include directive through which the file was reached.  This
; check validates each statement against the source documents of the include
; environment.  It never expands includes and never runs the include resolver:
; it follows an input's own occurrence back to the root file, and at every step
; it checks that the named record is an include directive of the file it names,
; that the file's include binding for that directive leads to the next
; document, that this document has the stated digest, and that the directive's
; selection admits the next record.  The input clause must then be the
; projection of the source record its occurrence names.
;
; That projection scopes a clause's variables by the clause's occurrence, and
; no two inputs may share an occurrence, so the checked inputs are
; standardized apart: two inputs never share a variable, including the two
; copies of a clause in a file included twice.
;
; A problem may leave out clauses of its sources; a refutation of some of the
; source clauses refutes them all.  Every clause it does contain must be one of
; them.
;
; A clause of a formula's clausal form is checked against the formula's
; recorded clausification.  The recorded clausifications must start from the
; empty Skolem and naming frontiers and each must continue from where the one
; before it stopped, so the symbols they introduce are fresh: no two formulas
; share a Skolem function or a definitional predicate.  Each one is then
; recomputed from the formula record its occurrence names, through the
; verified clausification stages, and must match what was recorded.  The
; clause must be the clause at its position in that recomputed clausal form.

; ------------------------------------------------------ source documents --

(= (tptp-resolution-example:provenance:document $source
      (tptp-ci-v1:documents-nil))
   (FOProvenance.NoDocument))

(= (tptp-resolution-example:provenance:document $source
      (tptp-ci-v1:documents-cons
        (tptp-ci-v1:source-document $candidate $digest $records $_text)
        $rest))
   (if (== $candidate $source)
       (FOProvenance.Document $candidate $digest $records)
       (tptp-resolution-example:provenance:document $source $rest)))

(= (tptp-resolution-example:provenance:binding $source $requested
      (tptp-ci-v1:bindings-nil))
   (FOProvenance.NoBinding))

(= (tptp-resolution-example:provenance:binding $source $requested
      (tptp-ci-v1:bindings-cons
        (tptp-ci-v1:include-binding $from $name $target $_mode)
        $rest))
   (if (== $from $source)
       (if (== $name $requested)
           (FOProvenance.Binding $target)
           (tptp-resolution-example:provenance:binding $source $requested $rest))
       (tptp-resolution-example:provenance:binding $source $requested $rest)))

(= (tptp-resolution-example:provenance:record-in $source $records $index)
   (if (< $index 0)
       (FOProvenance.Rejected SourceRecordMissing)
       (if (< $index (size-atom $records))
           (let $record (index-atom $records $index)
             (FOProvenance.Record $source $record))
           (FOProvenance.Rejected SourceRecordMissing))))

; An include directive of the compact form, with the records it selects.

(= (tptp-resolution-example:provenance:directive $record)
   (case $record
     (((incl $requested)
        (FOProvenance.Directive $requested (FOProvenance.All)))
      ((incl $requested $selection)
        (FOProvenance.Directive $requested (FOProvenance.Only $selection)))
      ((incl $requested $selection $_qualification)
        (FOProvenance.Directive $requested (FOProvenance.Only $selection)))
      ($_ (FOProvenance.NotADirective)))))

(= (tptp-resolution-example:provenance:member $item $items)
   (if (== (size-atom $items) 0)
       False
       (let ($first $rest) (decons-atom $items)
         (if (== $first $item)
             True
             (tptp-resolution-example:provenance:member $item $rest)))))

(= (tptp-resolution-example:provenance:record-name $record)
   (let ($_family $fields) (decons-atom $record)
     (let ($name $_rest) (decons-atom $fields)
       $name)))

(= (tptp-resolution-example:provenance:selected
      $selection (FOProvenance.Rejected $reason))
   (FOProvenance.Rejected $reason))

(= (tptp-resolution-example:provenance:selected
      $selection (FOProvenance.Record $source $record))
   (case $selection
     (((FOProvenance.All) (FOProvenance.Record $source $record))
      ((FOProvenance.Only $names)
        (if (tptp-resolution-example:provenance:member
              (tptp-resolution-example:provenance:record-name $record) $names)
            (FOProvenance.Record $source $record)
            (FOProvenance.Rejected RecordNotSelected))))))

; ------------------------------------------------------------ occurrences --
;
; The source record an occurrence names, with the source path of the document
; it stands in.  A root-level occurrence names a record of the root file; an
; included occurrence names a record of the file that the include directive at
; its `via` occurrence leads to.

(= (tptp-resolution-example:provenance:locate $environment $root-source $occurrence)
   (case $occurrence
     (((fo-resolution:occurrence (fo-resolution:source-digest $digest) $index)
        (tptp-resolution-example:provenance:locate-root
          $environment $root-source $digest $index))
      ((fo-resolution:included-occurrence
         (fo-resolution:source-digest $digest) $index $via)
        (tptp-resolution-example:provenance:locate-included
          $environment $digest $index
          (tptp-resolution-example:provenance:locate
            $environment $root-source $via)))
      ($_ (FOProvenance.Rejected OccurrenceMalformed)))))

(= (tptp-resolution-example:provenance:locate-root
      (tptp-ci-v1:environment $documents $_bindings) $root-source $digest $index)
   (case (tptp-resolution-example:provenance:document $root-source $documents)
     (((FOProvenance.Document $source $root-digest $records)
        (if (== $digest $root-digest)
            (tptp-resolution-example:provenance:record-in $source $records $index)
            (FOProvenance.Rejected RootOccurrenceOutsideRootFile)))
      ((FOProvenance.NoDocument)
        (FOProvenance.Rejected RootDocumentMissing)))))

(= (tptp-resolution-example:provenance:locate-included
      $environment $digest $index (FOProvenance.Rejected $reason))
   (FOProvenance.Rejected $reason))

(= (tptp-resolution-example:provenance:locate-included
      (tptp-ci-v1:environment $documents $bindings) $digest $index
      (FOProvenance.Record $parent $directive))
   (case (tptp-resolution-example:provenance:directive $directive)
     (((FOProvenance.Directive $requested $selection)
        (case (tptp-resolution-example:provenance:binding
                $parent $requested $bindings)
          (((FOProvenance.Binding $target)
             (tptp-resolution-example:provenance:included-record
               $documents $target $digest $index $selection))
           ((FOProvenance.NoBinding)
             (FOProvenance.Rejected IncludeBindingMissing)))))
      ((FOProvenance.NotADirective)
        (FOProvenance.Rejected ViaIsNotAnIncludeDirective)))))

(= (tptp-resolution-example:provenance:included-record
      $documents $target $digest $index $selection)
   (case (tptp-resolution-example:provenance:document $target $documents)
     (((FOProvenance.Document $source $target-digest $records)
        (if (== $digest $target-digest)
            (tptp-resolution-example:provenance:selected $selection
              (tptp-resolution-example:provenance:record-in
                $source $records $index))
            (FOProvenance.Rejected IncludedDigestMismatch)))
      ((FOProvenance.NoDocument)
        (FOProvenance.Rejected IncludedDocumentMissing)))))

; ------------------------------------------------------ clausifications --

(= (tptp-resolution-example:provenance:clausifications
      $program $environment $root-source $recorded $skolem $naming $seen $clausal)
   (if (== (size-atom $recorded) 0)
       (FOProvenance.Clausified $clausal)
       (let ($first $rest) (decons-atom $recorded)
         (tptp-resolution-example:provenance:clausification
           $program $environment $root-source $first $rest
           $skolem $naming $seen $clausal))))

(= (tptp-resolution-example:provenance:clausification
      $program $environment $root-source
      (TPTP:ClausificationV1 $occurrence $polarity
        $skolem-in $skolem-out $skolems $naming-in $naming-out $definitions)
      $rest $skolem $naming $seen $clausal)
   (if (tptp-resolution-example:provenance:member $occurrence $seen)
       (FOProvenance.Rejected FormulaClausifiedTwice)
       (if (== $skolem-in $skolem)
           (if (== $naming-in $naming)
               (case (tptp-resolution-example:provenance:locate
                       $environment $root-source $occurrence)
                 (((FOProvenance.Record $_source $record)
                    (tptp-resolution-example:provenance:recompute
                      $program $environment $root-source $occurrence $record
                      (TPTP:ClausificationV1 $occurrence $polarity
                        $skolem-in $skolem-out $skolems
                        $naming-in $naming-out $definitions)
                      $rest $seen $clausal))
                  ((FOProvenance.Rejected $reason) (FOProvenance.Rejected $reason))))
               (FOProvenance.Rejected DefinitionSymbolsNotFresh))
           (FOProvenance.Rejected SkolemSymbolsNotFresh))))

(= (tptp-resolution-example:provenance:recompute
      $program $environment $root-source $occurrence $record $recorded
      $rest $seen $clausal)
   (let (TPTP:ClausificationV1 $_occurrence $_polarity
          $skolem-in $_skolem-out $_skolems $naming-in $_naming-out $_definitions)
        $recorded
     (case (tptp:clausify-formula-with
             $program $occurrence $record $skolem-in $naming-in)
       (((TPTP:ClausifiedClausesV1 $recomputed $clauses)
          (if (== $recomputed $recorded)
              (let (TPTP:ClausificationV1 $_o $_p $_si $skolem-out $_s
                     $_ni $naming-out $_d)
                   $recomputed
                (let $next-seen (cons-atom $occurrence $seen)
                  (let $next-clausal
                       (cons-atom (FOProvenance.ClausalForm $occurrence $clauses) $clausal)
                    (tptp-resolution-example:provenance:clausifications
                      $program $environment $root-source $rest
                      $skolem-out $naming-out $next-seen $next-clausal))))
              (FOProvenance.Rejected ClausificationDoesNotMatch)))
        ($other (FOProvenance.Rejected (ClausificationFailed $other)))))))

(= (tptp-resolution-example:provenance:clausal-form $formula $clausal)
   (if (== (size-atom $clausal) 0)
       (FOProvenance.NotClausified)
       (let ((FOProvenance.ClausalForm $candidate $clauses) $rest) (decons-atom $clausal)
         (if (== $candidate $formula)
             (FOProvenance.Clauses $clauses)
             (tptp-resolution-example:provenance:clausal-form $formula $rest)))))

(= (tptp-resolution-example:provenance:clause-at (fo-resolution:clauses-nil) $occurrence)
   (FOProvenance.NoClause))

(= (tptp-resolution-example:provenance:clause-at
      (fo-resolution:clauses-cons
        (fo-resolution:clause $candidate $name $role $literals) $rest)
      $occurrence)
   (if (== $candidate $occurrence)
       (FOProvenance.Clause (fo-resolution:clause $candidate $name $role $literals))
       (tptp-resolution-example:provenance:clause-at $rest $occurrence)))

; ---------------------------------------------------------------- clauses --

(= (tptp-resolution-example:provenance:clause
      $program $environment $root-source $clausal
      (fo-resolution:clause $occurrence $name $role $literals))
   (case $occurrence
     (((fo-resolution:clausal-occurrence $formula $_index)
        (tptp-resolution-example:provenance:clausal-clause $clausal $formula
          (fo-resolution:clause $occurrence $name $role $literals)))
      ($_ (tptp-resolution-example:provenance:source-clause
            $program $environment $root-source
            (fo-resolution:clause $occurrence $name $role $literals))))))

(= (tptp-resolution-example:provenance:clausal-clause $clausal $formula
      (fo-resolution:clause $occurrence $name $role $literals))
   (case (tptp-resolution-example:provenance:clausal-form $formula $clausal)
     (((FOProvenance.Clauses $clauses)
        (case (tptp-resolution-example:provenance:clause-at $clauses $occurrence)
          (((FOProvenance.Clause $recomputed)
             (if (== $recomputed (fo-resolution:clause $occurrence $name $role $literals))
                 (FOProvenance.ClauseVerified)
                 (FOProvenance.Rejected ClauseIsNotInTheClausalForm)))
           ((FOProvenance.NoClause)
             (FOProvenance.Rejected ClauseIsNotInTheClausalForm)))))
      ((FOProvenance.NotClausified)
        (FOProvenance.Rejected ClausifiedFormulaNotRecorded)))))

(= (tptp-resolution-example:provenance:source-clause
      $program $environment $root-source
      (fo-resolution:clause $occurrence $name $role $literals))
   (case (tptp-resolution-example:provenance:locate
           $environment $root-source $occurrence)
     (((FOProvenance.Record $_source $record)
        (let $recomputed
             (tptp:record->resolution-clause-with $program $occurrence $record)
          (if (== $recomputed
                  (fo-resolution:clause $occurrence $name $role $literals))
              (FOProvenance.ClauseVerified)
              (FOProvenance.Rejected ClauseIsNotItsSourceRecord))))
      ((FOProvenance.Rejected $reason) (FOProvenance.Rejected $reason)))))

(= (tptp-resolution-example:provenance:clauses
      $program $environment $root-source $clausal (fo-resolution:clauses-nil) $seen)
   (FOProvenance.Verified))

(= (tptp-resolution-example:provenance:clauses
      $program $environment $root-source $clausal
      (fo-resolution:clauses-cons
        (fo-resolution:clause $occurrence $name $role $literals) $rest)
      $seen)
   (if (tptp-resolution-example:provenance:member $occurrence $seen)
       (FOProvenance.Rejected OccurrenceRepeated)
       (case (tptp-resolution-example:provenance:clause
               $program $environment $root-source $clausal
               (fo-resolution:clause $occurrence $name $role $literals))
         (((FOProvenance.ClauseVerified)
            (let $extended (cons-atom $occurrence $seen)
              (tptp-resolution-example:provenance:clauses
                $program $environment $root-source $clausal $rest $extended)))
          ((FOProvenance.Rejected $reason) (FOProvenance.Rejected $reason))))))

; ------------------------------------------------------------ entry point --

(= (tptp-resolution-example:provenance:check $loaded $problem)
   (tptp-resolution-example:provenance:check $loaded $problem ()))

; A problem whose formulas were clausified, with their recorded
; clausifications in input order.
(= (tptp-resolution-example:provenance:check $loaded $problem $clausifications)
   (case $loaded
     (((TPTP:CompactIncludeEnvironmentV1 $root-source $_library-root $environment)
        (let (fo-resolution:problem (fo-resolution:source-digest $digest) $clauses)
             $problem
          (let (tptp-ci-v1:environment $documents $_bindings) $environment
            (case (tptp-resolution-example:provenance:document
                    $root-source $documents)
              (((FOProvenance.Document $_source $root-digest $_records)
                 (if (== $digest $root-digest)
                     (tptp-resolution-example:provenance:with-view
                       $environment $root-source $clauses $clausifications)
                     (FOProvenance.Rejected ProblemSourceIsNotTheRootFile)))
               ((FOProvenance.NoDocument)
                 (FOProvenance.Rejected RootDocumentMissing)))))))
      ($_ (FOProvenance.Rejected NotAnIncludeEnvironment)))))

(= (tptp-resolution-example:provenance:with-view
      $environment $root-source $clauses $clausifications)
   (let $program (tptp:open-resolution-input-view)
     (case $program
       (((NativeHandle $_kind $_id)
          (let $outcome
               (case (tptp-resolution-example:provenance:clausifications
                       $program $environment $root-source $clausifications
                       (tptp-fof-resolved:index-zero) (tptp-fof-resolved:index-zero)
                       () ())
                 (((FOProvenance.Clausified $clausal)
                    (tptp-resolution-example:provenance:clauses
                      $program $environment $root-source $clausal $clauses ()))
                  ((FOProvenance.Rejected $reason) (FOProvenance.Rejected $reason))))
            (let $_ (langdef:close-deterministic-equations $program)
              $outcome)))
        ($fault (FOProvenance.Rejected (ProjectionUnavailable $fault)))))))
