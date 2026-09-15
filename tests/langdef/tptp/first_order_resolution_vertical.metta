!(import! &self ../../../langdef/logic/clause_data_to_resolution_input_v1.generated.metta)
; End-to-end first-order CNF semantic vertical.
;
; The generated clause-data projection is imported on the first line so that a
; mutation gate can substitute a recompiled program for it.
;
;   TPTP CNF source file
;     -> checked ParserPack parse            (langdef:prepare-import-file)
;     -> admitted FirstOrderClauseData       (tptp:parse->clause-data)
;     -> admitted FirstOrderResolutionInput  (fo-resolution:project-problem)
;     -> authored first-order resolution     (tptp-resolution-example:search:prove)
;     -> private example trace               (fo-proof:certificate)
;     -> independent trace check             (tptp-resolution-example:check:certificate)
;
; The positive end-to-end input is a `.p` file.  No `fo-cnf:problem`,
; `fo-resolution:problem`, or certificate is hand-built on the accepted path;
; the hand-built values below are adversarial controls only, and each one is
; expected to be rejected or to fail to refute.

!(import! &self langdef)
!(import! &self ../../../examples/atp/tptp_resolution/unification.metta)
!(import! &self ../../../examples/atp/tptp_resolution/search.metta)
!(import! &self ../../../examples/atp/tptp_resolution/trace_check.metta)

; ------------------------------------------------------------- pipeline ----

(= (test:clause-data $langdef $file)
   (let $prepared (langdef:prepare-import-file $langdef $file)
     (tptp:parse->clause-data $prepared)))

(= (test:problem $langdef $file)
   (let $clause-data (test:clause-data $langdef $file)
     (fo-resolution:project-problem $clause-data)))

(= (test:prove $problem)
   (tptp-resolution-example:search:prove $problem (FOSearch.Bounds 20 200)))

(= (test:certificate $result)
   (case $result
     (((FOSearch.Result (FOSearch.Refutation $certificate) $statistics)
        $certificate)
      ($_ (TestNoCertificate)))))

(= (test:outcome $result)
   (let (FOSearch.Result $outcome $statistics) $result $outcome))

(= (test:generated-count $result)
   (case $result
     (((FOSearch.Result $outcome (FOSearch.Stats $given $generated))
        $generated)
      ($_ -1))))

; The formula name and role authored in the source file must survive parsing
; and projection, so that an accepted refutation can be audited back to the
; clauses it came from.
(= (test:third-clause-provenance $problem)
   (case $problem
     (((fo-resolution:problem $source
         (fo-resolution:clauses-cons $first
           (fo-resolution:clauses-cons $second
             (fo-resolution:clauses-cons
               (fo-resolution:clause $occurrence $name $role $literals)
               $rest))))
        ($name $role))
      ($_ (TestNoThirdClause)))))

(= (test:rejected $result)
   (case $result
     (((LangDef:TermRejected $reason $message) True)
      ($_ False))))

; -------------------------------------------------- certificate surgery ----
;
; Adversarial controls rebuild one step of a genuine certificate.  Every
; mutation below is expected to be caught by the checker.

(= (test:first-step $certificate)
   (let (fo-proof:certificate $source $steps $empty) $certificate
     (let (fo-proof:steps-cons $first $rest) $steps $first)))

(= (test:replace-first-step $certificate $step)
   (let (fo-proof:certificate $source $steps $empty) $certificate
     (let (fo-proof:steps-cons $first $rest) $steps
       (let $replaced (fo-proof:steps-cons $step $rest)
         (fo-proof:certificate $source $replaced $empty)))))

(= (test:forge-conclusion $certificate $literals)
   (let (fo-proof:step $identifier $left $right $conclusion)
        (test:first-step $certificate)
     (let $forged (fo-proof:step $identifier $left $right $literals)
       (test:replace-first-step $certificate $forged))))

(= (test:replace-left-substitution $certificate $substitution)
   (let (fo-proof:step $identifier
          (fo-proof:parent $clause $index $previous) $right $conclusion)
        (test:first-step $certificate)
     (let $parent (fo-proof:parent $clause $index $substitution)
       (let $step (fo-proof:step $identifier $parent $right $conclusion)
         (test:replace-first-step $certificate $step)))))

(= (test:replace-right-substitution $certificate $substitution)
   (let (fo-proof:step $identifier $left
          (fo-proof:parent $clause $index $previous) $conclusion)
        (test:first-step $certificate)
     (let $parent (fo-proof:parent $clause $index $substitution)
       (let $step (fo-proof:step $identifier $left $parent $conclusion)
         (test:replace-first-step $certificate $step)))))

(= (test:replace-left-parent-clause $certificate $identifier)
   (let (fo-proof:step $step-identifier
          (fo-proof:parent $clause $index $substitution) $right $conclusion)
        (test:first-step $certificate)
     (let $parent (fo-proof:parent $identifier $index $substitution)
       (let $step
            (fo-proof:step $step-identifier $parent $right $conclusion)
         (test:replace-first-step $certificate $step)))))

(= (test:replace-step-identifier $certificate $identifier)
   (let (fo-proof:step $previous $left $right $conclusion)
        (test:first-step $certificate)
     (let $step (fo-proof:step $identifier $left $right $conclusion)
       (test:replace-first-step $certificate $step))))

(= (test:replace-source $certificate $source)
   (let (fo-proof:certificate $previous $steps $empty) $certificate
     (fo-proof:certificate $source $steps $empty)))

(= (test:replace-concluding-clause $certificate $identifier)
   (let (fo-proof:certificate $source $steps $previous) $certificate
     (fo-proof:certificate $source $steps $identifier)))

; --------------------------------------------------- control vocabulary ----

(= (test:control-variable $name)
   (fo-resolution:variable-id
     (fo-resolution:occurrence
       (fo-resolution:source-digest "control") 0)
     (fo-resolution:variable-name $name)))

(= (test:constant $name)
   (fo-resolution:term-function
     (fo-resolution:symbol-name (fo-resolution:symbol-lower) $name)
     (fo-resolution:terms-nil)))

(= (test:wrap $term)
   (fo-resolution:term-function
     (fo-resolution:symbol-name (fo-resolution:symbol-lower) "f")
     (fo-resolution:terms-cons $term (fo-resolution:terms-nil))))

(= (test:empty-substitution)
   (fo-proof:substitution (fo-proof:bindings-nil)))

; `X` bound to `f(X)`: a direct cycle.
(= (test:direct-cycle-substitution)
   (fo-proof:substitution
     (fo-proof:bindings-cons
       (fo-proof:binding (test:control-variable "X")
         (test:wrap
           (fo-resolution:term-variable (test:control-variable "X"))))
       (fo-proof:bindings-nil))))

; `X` bound to `f(Y)` while `Y` is itself bound: an indirect cycle, which is
; not a resolved unifier even though neither binding mentions its own key.
(= (test:indirect-cycle-substitution)
   (fo-proof:substitution
     (fo-proof:bindings-cons
       (fo-proof:binding (test:control-variable "X")
         (test:wrap
           (fo-resolution:term-variable (test:control-variable "Y"))))
       (fo-proof:bindings-cons
         (fo-proof:binding (test:control-variable "Y")
           (test:constant "a"))
         (fo-proof:bindings-nil)))))

(= (test:duplicate-key-substitution)
   (fo-proof:substitution
     (fo-proof:bindings-cons
       (fo-proof:binding (test:control-variable "X")
         (test:constant "a"))
       (fo-proof:bindings-cons
         (fo-proof:binding (test:control-variable "X")
           (test:constant "b"))
         (fo-proof:bindings-nil)))))

; A hand-built control in which the second clause carries a variable scoped to
; the FIRST clause's occurrence.  Distinct source occurrences are what keeps
; input clauses standardized apart; when that distinction is removed the two
; clauses share a variable and the search declines to resolve them.
(= (test:shared-scope-variable)
   (fo-resolution:variable-id
     (fo-resolution:occurrence
       (fo-resolution:source-digest "shared") 0)
     (fo-resolution:variable-name "X")))

(= (test:shared-scope-problem)
   (fo-resolution:problem
     (fo-resolution:source-digest "shared")
     (fo-resolution:clauses-cons
       (fo-resolution:clause
         (fo-resolution:occurrence
           (fo-resolution:source-digest "shared") 0)
         (fo-resolution:formula-name-integer "1")
         (fo-resolution:role "axiom")
         (fo-resolution:literals-cons
           (fo-resolution:literal-positive
             (fo-resolution:atom-predicate
               (fo-resolution:symbol-name
                 (fo-resolution:symbol-lower) "p")
               (fo-resolution:terms-cons
                 (fo-resolution:term-variable
                   (test:shared-scope-variable))
                 (fo-resolution:terms-nil))))
           (fo-resolution:literals-nil)))
       (fo-resolution:clauses-cons
         (fo-resolution:clause
           (fo-resolution:occurrence
             (fo-resolution:source-digest "shared") 1)
           (fo-resolution:formula-name-integer "2")
           (fo-resolution:role "negated_conjecture")
           (fo-resolution:literals-cons
             (fo-resolution:literal-negative
               (fo-resolution:atom-predicate
                 (fo-resolution:symbol-name
                   (fo-resolution:symbol-lower) "p")
                 (fo-resolution:terms-cons
                   (test:wrap
                     (fo-resolution:term-variable
                       (test:shared-scope-variable)))
                   (fo-resolution:terms-nil))))
             (fo-resolution:literals-nil)))
         (fo-resolution:clauses-nil)))))

; ------------------------------------------------------------- fixtures ----

!(bind! &tptp-langdef (langdef:load "langdef/tptp/langdef.metta"))
!(bind! &clause-data-language
   (langdef:load-language
     "langdef/logic/first_order_clause_data_v1.metta"))
!(bind! &resolution-input-language
   (langdef:load-language
     "langdef/logic/first_order_resolution_input_v1.metta"))
!(bind! &resolution-trace-language
   (langdef:load-language
     "examples/atp/tptp_resolution/first_order_resolution_trace_v1.metta"))

!(bind! &problem
   (test:problem &tptp-langdef
     "tests/langdef/tptp/nonground_refutation.p"))
!(bind! &result (test:prove &problem))
!(bind! &certificate (test:certificate &result))

; ---------------------------------------------------- admitted boundaries --

; 1: the projected problem is a value of the resolution-input language.
!(assertEqual
   (langdef:admit-term
     &resolution-input-language "Problem" &problem)
   (LangDef:TermAdmitted))

; 2: the private trace is a value of the example trace language.
!(assertEqual
   (langdef:admit-term
     &resolution-trace-language "Certificate" &certificate)
   (LangDef:TermAdmitted))

; 3: cross-language rejection -- the example trace language's own root is not a term
; of the resolution-input language.
!(assertEqual
   (test:rejected
     (langdef:admit-term
       &resolution-input-language "Problem" &certificate))
   True)

; 4: the example trace language is a conservative extension of the resolution-input
; language, so an input problem is admitted under it unchanged.  This is why a
; certificate quotes clause data instead of re-encoding it.
!(assertEqual
   (langdef:admit-term
     &resolution-trace-language "Problem" &problem)
   (LangDef:TermAdmitted))

; 5: a non-CNF source is not silently reinterpreted as CNF.
!(assertEqual
   (test:rejected
     (langdef:admit-term &clause-data-language "Problem"
       (test:clause-data &tptp-langdef
         "tests/langdef/tptp/fof_shadowing.p")))
   True)

; ------------------------------------------------------ accepted results --

; 6: the non-ground multistep refutation replays.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem &certificate)
   (FOCheck.Verified))

; 7: the refutation really needed more than one inference.
!(assertEqual (< 1 (test:generated-count &result)) True)

; 8: the ground problem refutes and replays on the same path.
!(assertEqual
   (let $ground-problem
        (test:problem &tptp-langdef
          "tests/langdef/tptp/ground_refutation.p")
     (tptp-resolution-example:check:certificate $ground-problem
       (test:certificate (test:prove $ground-problem))))
   (FOCheck.Verified))

; 9: a satisfiable problem returns an honest non-proof outcome.
!(assertEqual
   (test:outcome
     (test:prove
       (test:problem &tptp-langdef
         "tests/langdef/tptp/ground_satisfiable.p")))
   (FOSearch.Incomplete PassiveExhausted))

; 10: a step budget below the proof length returns an honest non-proof
; outcome rather than an invented refutation.
!(assertEqual
   (test:outcome
     (tptp-resolution-example:search:prove &problem (FOSearch.Bounds 1 200)))
   (FOSearch.Incomplete GivenBound))

; ------------------------------------------------- variable identity ------

; 11: the authored unifier's occurs check is load-bearing.
!(assertEqual
   (tptp-resolution-example:unify:unify-term ()
     (fo-resolution:term-variable (test:control-variable "X"))
     (test:wrap
       (fo-resolution:term-variable (test:control-variable "X"))))
   (FOUnify.Fail OccursCheck))

; 12: two clauses sharing a variable scope are not resolved.  Contrast with
; assertion 6: the accepted refutation exists precisely because the display
; name `X` in two source occurrences denotes two distinct variables.
!(assertEqual
   (test:outcome (test:prove (test:shared-scope-problem)))
   (FOSearch.Incomplete PassiveExhausted))

; ------------------------------------------------- rejected private traces --

; 13: a forged resolvent is recomputed and rejected.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:forge-conclusion &certificate
       (fo-resolution:literals-nil)))
   (FOCheck.Rejected ResolventDoesNotMatch))

; 14: dropping the substitution that instantiates a parent leaves the
; selected literals non-complementary.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-right-substitution &certificate
       (test:empty-substitution)))
   (FOCheck.Rejected SelectedLiteralsNotComplementary))

; 15: a direct occurs-check cycle is rejected.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-left-substitution &certificate
       (test:direct-cycle-substitution)))
   (FOCheck.Rejected LeftSubstitutionMalformed))

; 16: an indirect cycle through a second binding is rejected.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-left-substitution &certificate
       (test:indirect-cycle-substitution)))
   (FOCheck.Rejected LeftSubstitutionMalformed))

; 17: duplicate substitution keys are rejected.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-right-substitution &certificate
       (test:duplicate-key-substitution)))
   (FOCheck.Rejected RightSubstitutionMalformed))

; 18: a certificate for a different source is rejected.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-source &certificate
       (fo-resolution:source-digest "not-this-problem")))
   (FOCheck.Rejected SourceDigestMismatch))

; 19: citing a clause that no earlier step derived is rejected.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-left-parent-clause &certificate
       (fo-proof:derived-clause 9)))
   (FOCheck.Rejected ParentClauseNotDerived))

; 20: steps must conclude the clause identity they are checked against.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-step-identifier &certificate
       (fo-proof:derived-clause 9)))
   (FOCheck.Rejected StepIdentifierOutOfOrder))

; 21: the concluding clause must actually be empty.
!(assertEqual
   (tptp-resolution-example:check:certificate &problem
     (test:replace-concluding-clause &certificate
       (fo-proof:derived-clause 0)))
   (FOCheck.Rejected ConcludingClauseNotEmpty))

; 22: the source formula name and role reach the resolution input intact.
!(assertEqual
   (test:third-clause-provenance &problem)
   ((fo-resolution:formula-name-atomic
      (fo-resolution:symbol-name
        (fo-resolution:symbol-lower) "goal"))
    (fo-resolution:role "negated_conjecture")))

!(assertEqual (langdef:close-language &resolution-trace-language) True)
!(assertEqual (langdef:close-language &resolution-input-language) True)
!(assertEqual (langdef:close-language &clause-data-language) True)
!(assertEqual (langdef:close &tptp-langdef) True)

!(println! (FirstOrderResolutionVerticalV1Summary 22 22 0))
