; First-order formulas reach resolution through a checked clausification.
;
;   TPTP file of formulas and clauses
;     -> include environment, tagged policy
;     -> clausified resolution input        (tptp:include-environment->clausified-resolution-input)
;        each formula through the verified clausification stages, its
;        occurrence, polarity and Skolem and naming frontiers recorded
;     -> untrusted search                   (tptp-resolution-example:search:prove)
;     -> independent trace check            (tptp-resolution-example:check:certificate)
;     -> provenance and clausification      (tptp-resolution-example:provenance:check)
;
; Every forged input below is built from a genuine one by changing exactly one
; statement, and every forged clause is a genuine output of the clausification
; stages for what the forger claims.
;
; The real problem is SET915+1 of the pinned TPTP distribution, whose axioms
; and negated conjecture introduce Skolem constants.
;
; Arguments: the syllogism fixture, the two-witness fixture, the SET915+1
; file.

!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)
!(import! &self ../../../examples/atp/tptp_resolution/unification.metta)
!(import! &self ../../../examples/atp/tptp_resolution/search.metta)
!(import! &self ../../../examples/atp/tptp_resolution/trace_check.metta)
!(import! &self ../../../examples/atp/tptp_resolution/provenance_check.metta)

; ------------------------------------------------------------- pipeline ----

(= (test:environment $path)
   (tptp:load-compact-include-environment-with-reader
     (tptp:open-reader strict) tagged $path TPTP:NoLibraryRoot))

(= (test:input $environment)
   (tptp:include-environment->clausified-resolution-input $environment))

(= (test:problem (TPTP:ClausifiedResolutionInputV1 $problem $clausifications)) $problem)
(= (test:clausifications (TPTP:ClausifiedResolutionInputV1 $problem $clausifications))
   $clausifications)

(= (test:search $problem)
   (tptp-resolution-example:search:prove $problem (FOSearch.Bounds 40 2000)))

(= (test:outcome $result)
   (let (FOSearch.Result $outcome $statistics) $result $outcome))

(= (test:certificate $result)
   (case $result
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

(= (test:rebuild (fo-resolution:problem $source $clauses) $items)
   (fo-resolution:problem $source (test:clauses-of $items)))

(= (test:nth-clause $problem $index)
   (let $items (test:clauses $problem)
     (index-atom $items $index)))

(= (test:replace-nth $items $index $item)
   (let ($first $rest) (decons-atom $items)
     (if (== $index 0)
         (cons-atom $item $rest)
         (let $tail (test:replace-nth $rest (- $index 1) $item)
           (cons-atom $first $tail)))))

(= (test:occurrence (fo-resolution:clause $occurrence $name $role $literals))
   $occurrence)
(= (test:role (fo-resolution:clause $occurrence $name $role $literals)) $role)
(= (test:literals (fo-resolution:clause $occurrence $name $role $literals))
   $literals)
(= (test:with-literals (fo-resolution:clause $occurrence $name $role $previous) $literals)
   (fo-resolution:clause $occurrence $name $role $literals))

; The formula occurrence of a clause, or the clause's own source occurrence.
(= (test:origin $clause)
   (case (test:occurrence $clause)
     (((fo-resolution:clausal-occurrence $formula $index) (Clausal $formula))
      ($occurrence (Source $occurrence)))))

(= (test:origins $items)
   (if (== $items ())
       ()
       (let ($first $rest) (decons-atom $items)
         (let $tail (test:origins $rest)
           (let $origin (test:origin $first)
             (cons-atom $origin $tail))))))

; The clauses of one formula occurrence, and all clauses except them.
(= (test:clauses-of-formula $items $formula)
   (if (== $items ())
       ()
       (let ($first $rest) (decons-atom $items)
         (let $tail (test:clauses-of-formula $rest $formula)
           (if (== (test:origin $first) (Clausal $formula))
               (cons-atom $first $tail)
               $tail)))))

(= (test:clauses-not-of-formula $items $formula)
   (if (== $items ())
       ()
       (let ($first $rest) (decons-atom $items)
         (let $tail (test:clauses-not-of-formula $rest $formula)
           (if (== (test:origin $first) (Clausal $formula))
               $tail
               (cons-atom $first $tail))))))

(= (test:host-clauses (fo-resolution:clauses-nil)) ())
(= (test:host-clauses (fo-resolution:clauses-cons $clause $rest))
   (let $tail (test:host-clauses $rest)
     (cons-atom $clause $tail)))

; Symbols a set of clauses mentions under a given introduced kind.
(= (test:kind-symbols-term $kind $term $seen)
   (case $term
     (((fo-resolution:term-function (fo-resolution:symbol-name $candidate $text) $arguments)
        (let $after (test:kind-symbols-terms $kind $arguments $seen)
          (if (== $candidate $kind)
              (if (test:member $text $after) $after (cons-atom $text $after))
              $after)))
      ($_ $seen))))

(= (test:kind-symbols-terms $kind (fo-resolution:terms-nil) $seen) $seen)
(= (test:kind-symbols-terms $kind (fo-resolution:terms-cons $term $rest) $seen)
   (let $after (test:kind-symbols-term $kind $term $seen)
     (test:kind-symbols-terms $kind $rest $after)))

(= (test:kind-symbols-atom $kind $atom $seen)
   (case $atom
     (((fo-resolution:atom-predicate (fo-resolution:symbol-name $candidate $text) $arguments)
        (let $after (test:kind-symbols-terms $kind $arguments $seen)
          (if (== $candidate $kind)
              (if (test:member $text $after) $after (cons-atom $text $after))
              $after)))
      ((fo-resolution:atom-equality $left $right)
        (let $after (test:kind-symbols-term $kind $left $seen)
          (test:kind-symbols-term $kind $right $after))))))

(= (test:kind-symbols-literals $kind (fo-resolution:literals-nil) $seen) $seen)
(= (test:kind-symbols-literals $kind (fo-resolution:literals-cons $literal $rest) $seen)
   (let $atom (case $literal (((fo-resolution:literal-positive $a) $a)
                              ((fo-resolution:literal-negative $a) $a)))
     (let $after (test:kind-symbols-atom $kind $atom $seen)
       (test:kind-symbols-literals $kind $rest $after))))

(= (test:kind-symbols $kind $items $seen)
   (if (== $items ())
       $seen
       (let ($first $rest) (decons-atom $items)
         (let $after (test:kind-symbols-literals $kind (test:literals $first) $seen)
           (test:kind-symbols $kind $rest $after)))))

; Sequence primitives take their arguments unevaluated; these helpers give
; them values.
(= (test:length $items) (size-atom $items))
(= (test:element $items $index) (index-atom $items $index))
(= (test:append $left $right) (union-atom $left $right))
(= (test:singleton $item) (cons-atom $item ()))

(= (test:member $item $items)
   (if (== (size-atom $items) 0)
       False
       (let ($first $rest) (decons-atom $items)
         (if (== $first $item) True (test:member $item $rest)))))

(= (test:all-roles $items $role)
   (if (== $items ())
       True
       (let ($first $rest) (decons-atom $items)
         (if (== (test:role $first) $role)
             (test:all-roles $rest $role)
             False))))

; ------------------------------------------------------- source records ----

(= (test:root-digest (TPTP:CompactIncludeEnvironmentV1 $root $library
                       (tptp-ci-v1:environment $documents $bindings)))
   (case (tptp:include-environment-document $root
           (tptp-ci-v1:environment $documents $bindings))
     (((TPTP:IncludeEnvironmentDocumentV1 $source $digest $records) $digest))))

(= (test:root-record (TPTP:CompactIncludeEnvironmentV1 $root $library
                       (tptp-ci-v1:environment $documents $bindings))
                     $index)
   (case (tptp:include-environment-document $root
           (tptp-ci-v1:environment $documents $bindings))
     (((TPTP:IncludeEnvironmentDocumentV1 $source $digest $records)
        (index-atom $records $index)))))

(= (test:root-occurrence $environment $index)
   (fo-resolution:occurrence
     (fo-resolution:source-digest (test:root-digest $environment)) $index))

; ------------------------------------------------ clausification surgery --

(= (test:index-zero) (tptp-fof-resolved:index-zero))

; A formula record clausified from chosen frontiers, as a genuine forger would
; produce it: the clausification record and its resolution clauses.
(= (test:clausify $view $environment $index $skolem $naming)
   (tptp:clausify-formula-with $view (test:root-occurrence $environment $index)
     (test:root-record $environment $index) $skolem $naming))

(= (test:clausification-of (TPTP:ClausifiedClausesV1 $clausification $clauses))
   $clausification)
(= (test:resolution-clauses-of (TPTP:ClausifiedClausesV1 $clausification $clauses))
   (test:host-clauses $clauses))

; A conjecture clausified as if it were asserted: the verified stages with
; the positive polarity.
(= (test:clausify-asserted $view $environment $index $skolem $naming)
   (let $record (test:root-record $environment $index)
     (let $occurrence (test:root-occurrence $environment $index)
       (case (langdef:run-deterministic-equations $view
               (LangDef:DeterministicCall tptp-res-v1:clausification-request $record))
         (((TPTP:ClausificationRequestV1 $polarity $named $names)
            (case (tptp:clausify-named-formula $occurrence (tptp-fof-batch:positive)
                    $named (tptp:binder-equality-rows $names) $skolem $naming)
              (((TPTP:ClausifiedFormulaV1 $clausification $clauses)
                 (let (TPTP:NameAndRole $family $name $role) (tptp:record-name-and-role $record)
                   (case (langdef:run-deterministic-equations $view
                           (LangDef:DeterministicCall tptp-res-v1:clausal-form
                             $occurrence $name $role (tptp-fof-batch:positive) $clauses))
                     (((tptp-res-v1:ok $resolution-clauses)
                        (TPTP:ClausifiedClausesV1 $clausification $resolution-clauses))))))))))))))

(= (test:replace-clausification $clausifications $index $clausification)
   (test:replace-nth $clausifications $index $clausification))

(= (test:with-naming-start
      (TPTP:ClausificationV1 $occurrence $polarity $skolem-in $skolem-out $skolems
        $naming-in $naming-out $definitions)
      $start)
   (TPTP:ClausificationV1 $occurrence $polarity $skolem-in $skolem-out $skolems
     $start $naming-out $definitions))

(= (test:without-nth $items $index)
   (let ($first $rest) (decons-atom $items)
     (if (== $index 0)
         $rest
         (let $tail (test:without-nth $rest (- $index 1))
           (cons-atom $first $tail)))))

; ------------------------------------------------------------- fixtures ----

!(bind! &view (tptp:open-resolution-input-view))

!(bind! &set-environment (test:environment (system:arg 2)))
!(bind! &set-input (test:input &set-environment))
!(bind! &set (test:problem &set-input))

!(bind! &syllogism-environment (test:environment (system:arg 0)))
!(bind! &syllogism-input (test:input &syllogism-environment))
!(bind! &syllogism (test:problem &syllogism-input))
!(bind! &syllogism-clausifications (test:clausifications &syllogism-input))
!(bind! &syllogism-result (test:search &syllogism))

!(bind! &witnesses-environment (test:environment (system:arg 1)))
!(bind! &witnesses-input (test:input &witnesses-environment))
!(bind! &witnesses (test:problem &witnesses-input))
!(bind! &witnesses-clausifications (test:clausifications &witnesses-input))

; ------------------------------------------------ a clausified syllogism ----

; 1: four formulas are clausified; the clause keeps its source occurrence.
!(assertEqual
   ((test:length &syllogism-clausifications)
    (test:member (Source (test:root-occurrence &syllogism-environment 2))
      (test:origins (test:clauses &syllogism))))
   (4 True))

; 2: the existential introduces one Skolem constant and each implication one
; definitional predicate, each under its own symbol kind.
!(assertEqual
   ((test:kind-symbols (fo-resolution:symbol-skolem) (test:clauses &syllogism) ())
    (test:length (test:kind-symbols (fo-resolution:symbol-definition)
                   (test:clauses &syllogism) ())))
   (("0") 2))

; 3: the conjecture's clauses are those of its negation.
!(assertEqual
   (test:all-roles
     (test:clauses-of-formula (test:clauses &syllogism)
       (test:root-occurrence &syllogism-environment 4))
     (fo-resolution:role "negated_conjecture"))
   True)

; 4: the untrusted search refutes it and the independent checker replays the
; certificate.
!(assertEqual
   (tptp-resolution-example:check:certificate &syllogism
     (test:certificate &syllogism-result))
   (FOCheck.Verified))

; 5: every clause is a clause of its source, and every clausification is
; fresh and recomputes.
!(assertEqual
   (tptp-resolution-example:provenance:check &syllogism-environment &syllogism
     &syllogism-clausifications)
   (FOProvenance.Verified))

; --------------------------------------------------------- a real problem --

; 6: SET915+1 refutes through its clausification, the certificate replays,
; and every clause and clausification of the problem checks.
!(assertEqual
   ((tptp-resolution-example:check:certificate &set
      (test:certificate (test:search &set)))
    (tptp-resolution-example:provenance:check &set-environment &set
      (test:clausifications &set-input))
    (test:length (test:kind-symbols (fo-resolution:symbol-skolem) (test:clauses &set) ())))
   ((FOCheck.Verified) (FOProvenance.Verified) 4))

; --------------------------------------------- two witnesses: freshness ----

; 7: the two existentials get two Skolem constants, and the satisfiable
; problem is reported as not refuted.
!(assertEqual
   ((test:kind-symbols (fo-resolution:symbol-skolem) (test:clauses &witnesses) ())
    (test:outcome (test:search &witnesses))
    (tptp-resolution-example:provenance:check &witnesses-environment &witnesses
      &witnesses-clausifications))
   (("1" "0") (FOSearch.Incomplete PassiveExhausted) (FOProvenance.Verified)))

; 8: a forger clausifies the second formula from the empty Skolem frontier, so
; both formulas are witnessed by one constant.  The clauses are genuine
; outputs of the clausification stages, and they refute: the certificate
; replays.  Only freshness rejects the input, which here is unsound.
!(bind! &reused
   (test:clausify &view &witnesses-environment 1 (test:index-zero) (test:index-zero)))
!(bind! &forged-witnesses
   (test:rebuild &witnesses
     (test:append
       (test:clauses-not-of-formula (test:clauses &witnesses)
         (test:root-occurrence &witnesses-environment 1))
       (test:resolution-clauses-of &reused))))
!(bind! &forged-witnesses-clausifications
   (test:replace-clausification &witnesses-clausifications 1
     (test:clausification-of &reused)))
!(assertEqual
   ((tptp-resolution-example:check:certificate &forged-witnesses
      (test:certificate (test:search &forged-witnesses)))
    (tptp-resolution-example:provenance:check &witnesses-environment
      &forged-witnesses &forged-witnesses-clausifications))
   ((FOCheck.Verified) (FOProvenance.Rejected SkolemSymbolsNotFresh)))

; ------------------------------------------------ forged clausification ----

; 9: the conjecture asserted instead of negated.
!(bind! &asserted
   (test:clausify-asserted &view &syllogism-environment 4
     (tptp-fof-resolved:index-succ (tptp-fof-resolved:index-zero))
     (tptp-fof-resolved:index-succ
       (tptp-fof-resolved:index-succ (tptp-fof-resolved:index-zero)))))
!(assertEqual
   (tptp-resolution-example:provenance:check &syllogism-environment
     (test:rebuild &syllogism
       (test:append
         (test:clauses-not-of-formula (test:clauses &syllogism)
           (test:root-occurrence &syllogism-environment 4))
         (test:resolution-clauses-of &asserted)))
     (test:replace-clausification &syllogism-clausifications 3
       (test:clausification-of &asserted)))
   (FOProvenance.Rejected ClausificationDoesNotMatch))

; 10: a clause of the clausal form replaced by another clause's literals.
!(assertEqual
   (tptp-resolution-example:provenance:check &syllogism-environment
     (test:rebuild &syllogism
       (test:replace-nth (test:clauses &syllogism) 0
         (test:with-literals (test:nth-clause &syllogism 0)
           (test:literals (test:nth-clause &syllogism 1)))))
     &syllogism-clausifications)
   (FOProvenance.Rejected ClauseIsNotInTheClausalForm))

; 11: clauses of a formula whose clausification is not recorded.
!(assertEqual
   (tptp-resolution-example:provenance:check &syllogism-environment &syllogism
     (test:without-nth &syllogism-clausifications 3))
   (FOProvenance.Rejected ClausifiedFormulaNotRecorded))

; 12: a later formula starting from the empty naming frontier, so that its
; definitional predicate would be the first formula's.
!(assertEqual
   (tptp-resolution-example:provenance:check &syllogism-environment &syllogism
     (test:replace-clausification &syllogism-clausifications 2
       (test:with-naming-start (test:element &syllogism-clausifications 2)
         (test:index-zero))))
   (FOProvenance.Rejected DefinitionSymbolsNotFresh))

; 13: one formula clausified twice.
!(assertEqual
   (tptp-resolution-example:provenance:check &syllogism-environment &syllogism
     (test:append &syllogism-clausifications
       (test:singleton (test:element &syllogism-clausifications 0))))
   (FOProvenance.Rejected FormulaClausifiedTwice))

!(assertEqual (langdef:close-deterministic-equations &view) True)

!(println! (CompactResolutionClausificationV1Summary 14 14 0))
