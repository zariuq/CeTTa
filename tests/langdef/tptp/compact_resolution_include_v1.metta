; First-order CNF resolution across include chains, on the compact API.
;
;   root TPTP file and the files it includes
;     -> include environment, tagged policy (tptp:load-compact-include-environment-with-reader)
;     -> admitted resolution input          (tptp:include-environment->resolution-input)
;     -> untrusted search                   (tptp-resolution-example:search:prove)
;     -> independent trace check            (tptp-resolution-example:check:certificate)
;     -> source and include provenance      (tptp-resolution-example:provenance:check)
;
; The real problem is GRP041-2 of the pinned TPTP distribution, whose axioms
; come from an included axiom file.  The fixture chain includes one file twice,
; and that file includes a third with a selection.  Every forged problem below
; is built from a genuine one by changing exactly one provenance statement.
;
; Arguments: the fixture root, the GRP041-2 file, the TPTP library root.

!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)
!(import! &self ../../../examples/atp/tptp_resolution/unification.metta)
!(import! &self ../../../examples/atp/tptp_resolution/search.metta)
!(import! &self ../../../examples/atp/tptp_resolution/trace_check.metta)
!(import! &self ../../../examples/atp/tptp_resolution/provenance_check.metta)

; ------------------------------------------------------------- pipeline ----

(= (test:environment $root $library)
   (tptp:load-compact-include-environment-with-reader
     (tptp:open-reader strict) tagged $root $library))

(= (test:problem $environment)
   (case (tptp:include-environment->resolution-input $environment)
     (((TPTP:StageValue resolution-input $problem) $problem)
      ($other (TestNoProblem $other)))))

(= (test:certificate $problem)
   (case (tptp-resolution-example:search:prove $problem (FOSearch.Bounds 20 200))
     (((FOSearch.Result (FOSearch.Refutation $certificate) $statistics)
        $certificate)
      ($other (TestNoCertificate $other)))))

; ------------------------------------------------------- problem clauses ---

(= (test:clause-list (fo-resolution:clauses-nil)) ())
(= (test:clause-list (fo-resolution:clauses-cons $clause $rest))
   (let $tail (test:clause-list $rest)
     (cons-atom $clause $tail)))

(= (test:clauses-of $items)
   (if (== $items ())
       (fo-resolution:clauses-nil)
       (let ($first $rest) (decons-atom $items)
         (fo-resolution:clauses-cons $first (test:clauses-of $rest)))))

(= (test:clauses (fo-resolution:problem $source $clauses))
   (test:clause-list $clauses))

(= (test:source (fo-resolution:problem (fo-resolution:source-digest $digest) $clauses))
   $digest)

(= (test:rebuild (fo-resolution:problem $source $clauses) $items)
   (fo-resolution:problem $source (test:clauses-of $items)))

(= (test:with-source (fo-resolution:problem $previous $clauses) $digest)
   (fo-resolution:problem (fo-resolution:source-digest $digest) $clauses))

(= (test:replace-nth $items $index $item)
   (let ($first $rest) (decons-atom $items)
     (if (== $index 0)
         (cons-atom $item $rest)
         (let $tail (test:replace-nth $rest (- $index 1) $item)
           (cons-atom $first $tail)))))

(= (test:append-item $items $item)
   (if (== $items ())
       (cons-atom $item ())
       (let ($first $rest) (decons-atom $items)
         (let $tail (test:append-item $rest $item)
           (cons-atom $first $tail)))))

(= (test:nth $problem $index)
   (let $items (test:clauses $problem)
     (index-atom $items $index)))

(= (test:occurrence (fo-resolution:clause $occurrence $name $role $literals))
   $occurrence)
(= (test:literals (fo-resolution:clause $occurrence $name $role $literals))
   $literals)
(= (test:name
      (fo-resolution:clause $occurrence
        (fo-resolution:formula-name-atomic (fo-resolution:symbol-name $kind $text))
        $role $literals))
   $text)
(= (test:with-occurrence (fo-resolution:clause $previous $name $role $literals) $occurrence)
   (fo-resolution:clause $occurrence $name $role $literals))
(= (test:with-literals (fo-resolution:clause $occurrence $name $role $previous) $literals)
   (fo-resolution:clause $occurrence $name $role $literals))

(= (test:names $items)
   (if (== $items ())
       ()
       (let ($first $rest) (decons-atom $items)
         (let $tail (test:names $rest)
           (let $name (test:name $first)
             (cons-atom $name $tail))))))

(= (test:digest (fo-resolution:included-occurrence (fo-resolution:source-digest $digest) $index $via))
   $digest)
(= (test:via (fo-resolution:included-occurrence $source $index $via)) $via)
(= (test:included (fo-resolution:included-occurrence $source $index $via)) True)
(= (test:included (fo-resolution:occurrence $source $index)) False)

(= (test:count $problem)
   (let $items (test:clauses $problem)
     (size-atom $items)))

(= (test:included-count $items)
   (if (== $items ())
       0
       (let ($first $rest) (decons-atom $items)
         (if (test:included (test:occurrence $first))
             (+ 1 (test:included-count $rest))
             (test:included-count $rest)))))

; The variable of a clause's first literal's first argument.
(= (test:first-variable $clause)
   (case (test:literals $clause)
     (((fo-resolution:literals-cons
         ($_polarity
           (fo-resolution:atom-predicate $predicate
             (fo-resolution:terms-cons (fo-resolution:term-variable $variable) $terms)))
         $rest)
        $variable)
      ($_ (TestNoVariable)))))

; ---------------------------------------------------- certificate parents --

(= (test:parents-cite-included (fo-proof:steps-nil)) False)
(= (test:parents-cite-included
      (fo-proof:steps-cons
        (fo-proof:step $identifier
          (fo-proof:parent $left $left-index $left-substitution)
          (fo-proof:parent $right $right-index $right-substitution)
          $literals)
        $rest))
   (if (test:cites-included $left)
       True
       (if (test:cites-included $right)
           True
           (test:parents-cite-included $rest))))

(= (test:cites-included $clause)
   (case $clause
     (((fo-proof:input-clause $occurrence) (test:included $occurrence))
      ($_ False))))

(= (test:certificate-cites-included (fo-proof:certificate $source $steps $empty))
   (test:parents-cite-included $steps))

(= (test:replace-first-right-parent-clause $certificate $identifier)
   (let (fo-proof:certificate $source (fo-proof:steps-cons $first $rest) $empty)
        $certificate
     (let (fo-proof:step $step-identifier $left
            (fo-proof:parent $clause $index $substitution) $literals)
          $first
       (let $parent (fo-proof:parent $identifier $index $substitution)
         (let $step (fo-proof:step $step-identifier $left $parent $literals)
           (let $steps (fo-proof:steps-cons $step $rest)
             (fo-proof:certificate $source $steps $empty)))))))

; ------------------------------------------------------- source records ----

(= (test:record (TPTP:CompactIncludeEnvironmentV1 $root $library
                  (tptp-ci-v1:environment $documents $bindings))
                $digest $index)
   (test:record-in $documents $digest $index))
(= (test:record-in
      (tptp-ci-v1:documents-cons
        (tptp-ci-v1:source-document $source $candidate $records $text) $rest)
      $digest $index)
   (if (== $candidate $digest)
       (index-atom $records $index)
       (test:record-in $rest $digest $index)))

; ------------------------------------------------------------- fixtures ----

!(bind! &fixture-environment (test:environment (system:arg 0) TPTP:NoLibraryRoot))
!(bind! &fixture (test:problem &fixture-environment))
!(bind! &fixture-certificate (test:certificate &fixture))

; The fixture problem is base, step, base, step, goal: the first include of
; the middle file, then the second, then the root's own clause.
!(bind! &root-digest (test:source &fixture))
!(bind! &middle-digest (test:digest (test:occurrence (test:nth &fixture 1))))
!(bind! &leaf-digest (test:digest (test:occurrence (test:nth &fixture 0))))

!(bind! &grp-environment
   (test:environment (system:arg 1) (TPTP:LibraryRoot (system:arg 2))))
!(bind! &grp (test:problem &grp-environment))
!(bind! &grp-certificate (test:certificate &grp))

!(bind! &view (tptp:open-resolution-input-view))

; ------------------------------------------------- a real include chain ----

; 1: GRP041-2 states one negated conjecture and includes seven axioms, each of
; which is reached through the root's include directive.
!(assertEqual
   ((test:count &grp) (test:included-count (test:clauses &grp)))
   (8 7))

; 2: the included axioms' occurrences name the axiom file, not the root.
!(assertEqual
   (== (test:digest (test:occurrence (test:nth &grp 0))) (test:source &grp))
   False)

; 3: the untrusted search refutes it and the independent checker replays the
; certificate.
!(assertEqual
   (tptp-resolution-example:check:certificate &grp &grp-certificate)
   (FOCheck.Verified))

; 4: the refutation uses an included axiom.
!(assertEqual (test:certificate-cites-included &grp-certificate) True)

; 5: every input clause is the projection of the source record its
; occurrence names, through the include chain its occurrence names.
!(assertEqual
   (tptp-resolution-example:provenance:check &grp-environment &grp)
   (FOProvenance.Verified))

; ----------------------------------------- nesting, selection, duplicates --

; 6: the middle file is included twice, so its clause and the selected leaf
; clause occur twice; the unselected leaf clause does not occur.
!(assertEqual
   (test:names (test:clauses &fixture))
   ("base" "step" "base" "step" "goal"))

; 7: the two copies of one clause are two inputs with distinct variables.
!(assertEqual
   ((== (test:occurrence (test:nth &fixture 1)) (test:occurrence (test:nth &fixture 3)))
    (== (test:first-variable (test:nth &fixture 1))
        (test:first-variable (test:nth &fixture 3))))
   (False False))

; 8: the nested chain names the leaf file, the middle file's directive and the
; root's directive.
!(assertEqual
   (test:occurrence (test:nth &fixture 2))
   (fo-resolution:included-occurrence (fo-resolution:source-digest &leaf-digest) 0
     (fo-resolution:included-occurrence (fo-resolution:source-digest &middle-digest) 0
       (fo-resolution:occurrence (fo-resolution:source-digest &root-digest) 1))))

; 9: provenance holds and the refutation replays.
!(assertEqual
   ((tptp-resolution-example:provenance:check &fixture-environment &fixture)
    (tptp-resolution-example:check:certificate &fixture &fixture-certificate))
   ((FOProvenance.Verified) (FOCheck.Verified)))

; ------------------------------------------------------ forged provenance --

; 10: a clause whose literals are not those of the record its occurrence names.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture
       (test:replace-nth (test:clauses &fixture) 0
         (test:with-literals (test:nth &fixture 0)
           (test:literals (test:nth &fixture 4))))))
   (FOProvenance.Rejected ClauseIsNotItsSourceRecord))

; 11: an include chain through a record that is not an include directive.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture
       (test:replace-nth (test:clauses &fixture) 1
         (test:with-occurrence (test:nth &fixture 1)
           (fo-resolution:included-occurrence
             (fo-resolution:source-digest &middle-digest) 1
             (fo-resolution:occurrence (fo-resolution:source-digest &root-digest) 2))))))
   (FOProvenance.Rejected ViaIsNotAnIncludeDirective))

; 12: a genuine record of the leaf file that its include selection leaves
; out, with a genuine chain and its genuine projection.
!(bind! &unused-occurrence
   (fo-resolution:included-occurrence (fo-resolution:source-digest &leaf-digest) 1
     (fo-resolution:included-occurrence (fo-resolution:source-digest &middle-digest) 0
       (fo-resolution:occurrence (fo-resolution:source-digest &root-digest) 0))))
!(bind! &unused
   (tptp:record->resolution-clause-with &view &unused-occurrence
     (test:record &fixture-environment &leaf-digest 1)))
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture (test:append-item (test:clauses &fixture) &unused)))
   (FOProvenance.Rejected RecordNotSelected))

; 13: the two copies of the step clause merged into one occurrence.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture
       (test:replace-nth (test:clauses &fixture) 3
         (test:with-occurrence (test:nth &fixture 3)
           (test:occurrence (test:nth &fixture 1))))))
   (FOProvenance.Rejected OccurrenceRepeated))

; 14: an included clause attributed to a file other than the one its include
; directive leads to.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture
       (test:replace-nth (test:clauses &fixture) 0
         (test:with-occurrence (test:nth &fixture 0)
           (fo-resolution:included-occurrence
             (fo-resolution:source-digest &middle-digest) 0
             (test:via (test:occurrence (test:nth &fixture 0))))))))
   (FOProvenance.Rejected IncludedDigestMismatch))

; 15: a position past the end of the file.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture
       (test:replace-nth (test:clauses &fixture) 1
         (test:with-occurrence (test:nth &fixture 1)
           (fo-resolution:included-occurrence
             (fo-resolution:source-digest &middle-digest) 7
             (test:via (test:occurrence (test:nth &fixture 1))))))))
   (FOProvenance.Rejected SourceRecordMissing))

; 16: an included clause claimed as a clause of the root file.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture
       (test:replace-nth (test:clauses &fixture) 0
         (test:with-occurrence (test:nth &fixture 0)
           (fo-resolution:occurrence (fo-resolution:source-digest &leaf-digest) 0)))))
   (FOProvenance.Rejected RootOccurrenceOutsideRootFile))

; 17: a problem whose source is not the root file.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:with-source &fixture &leaf-digest))
   (FOProvenance.Rejected ProblemSourceIsNotTheRootFile))

; 18: a certificate citing an input the problem does not contain.
!(assertEqual
   (tptp-resolution-example:check:certificate &fixture
     (test:replace-first-right-parent-clause &fixture-certificate
       (fo-proof:input-clause &unused-occurrence)))
   (FOCheck.Rejected ParentClauseNotDerived))

; 19: an input carrying the variables of the other copy of its clause: the
; two inputs would share a variable, so they are not standardized apart.
!(assertEqual
   (tptp-resolution-example:provenance:check &fixture-environment
     (test:rebuild &fixture
       (test:replace-nth (test:clauses &fixture) 1
         (test:with-literals (test:nth &fixture 1)
           (test:literals (test:nth &fixture 3))))))
   (FOProvenance.Rejected ClauseIsNotItsSourceRecord))

!(assertEqual (langdef:close-deterministic-equations &view) True)

!(println! (CompactResolutionIncludeV1Summary 19 19 0))
