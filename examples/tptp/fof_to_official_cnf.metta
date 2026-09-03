; A concrete official TPTP FOF formula traverses the declared transformation
; LanguageDefs.  Each stage returns the complete relational result rather than
; selecting an arbitrary first reduct.

!(import! &self lib_tptp)

(= (example:official-truth)
   (tptp92-ast:fof-formula:alt-1
     (tptp92-ast:fof-logic-formula:alt-3
       (tptp92-ast:fof-unitary-formula:alt-2
         (tptp92-ast:fof-atomic-formula:alt-2
           (tptp92-ast:fof-defined-atomic-formula:alt-1
             (tptp92-ast:fof-defined-plain-formula:alt-1
               (tptp92-ast:fof-defined-plain-term:alt-1
                 (tptp92-ast:defined-constant:alt-1
                   (tptp92-ast:defined-functor:alt-1
                     (tptp92-ast:atomic-defined-word:alt-1
                       (tptp92-ast:token:dollar-word "$true"))))))))))))

(= (example:only-target $result)
   (case $result
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult $target $trace))
        $target)
      ($_ (TPTPFofCnfExample.ExpectedOneReduct $result)))))

(= (example:prenex-form $result)
   (case $result
     (((tptp-fof-prenex-normalize:result $source $form) $form)
      ($_ (TPTPFofCnfExample.ExpectedPrenexResult $result)))))

(= (example:skolem-form $result)
   (case $result
     (((tptp-fof-skolemize:form-result
         $environment $depth $frontier $source $target $next $introduced)
        $target)
      ($_ (TPTPFofCnfExample.ExpectedSkolemResult $result)))))

(= (example:skolem-output $result)
   (case $result
     (((tptp-fof-skolemize:form-result
         $environment $depth $frontier $source $target $next $introduced)
        (tptp-fof-skolem:output $target $next $introduced))
      ($_ (TPTPFofCnfExample.ExpectedSkolemResult $result)))))

(= (example:named-output $result)
   (case $result
     (((tptp-fof-name:open-result $depth $frontier $source $target)
        $target)
      ($_ (TPTPFofCnfExample.ExpectedNamingResult $result)))))

(= (example:cnf-output $result)
   (case $result
     (((tptp-fof-cnf-gen:generate-result $named $target) $target)
      ($_ (TPTPFofCnfExample.ExpectedCnfResult $result)))))

(= (example:official-cnf-summary $result)
   (case $result
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult
           (tptp-cnf-official-serialization:output
             $source
             (tptp-fof-batch:positive)
             (tptp-cnf-official-serialization:entries-cons
               (tptp-cnf-official-serialization:entry
                 $identity
                 (tptp92-ast:name:alt-1
                   (tptp92-ast:atomic-word:alt-2
                     (tptp92-ast:token:single-quoted "truth_clause")))
                 $clause $annotated)
               (tptp-cnf-official-serialization:entries-nil)))
           (LangDef:RuleTrace $start-index
             "tptp-cnf-official-serialization:start"
             (LangDef:CongruenceEvidence 0
               (LangDef:RuleTrace $entries-index
                 "tptp-cnf-official-serialization:entries-positive"
                 (LangDef:RelationEvidence
                   0 LangDef:ExternalRelation $row-index $receipt)
                 $clause-evidence $rest-evidence)))))
        (TPTPFofCnfExample.OfficialCnf "truth_clause" $receipt))
      ($_ (TPTPFofCnfExample.SerializationFailed $result)))))

(= (example:duplicate-resolution-summary $result)
   (case $result
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult $target $trace-aa)
         (LangDef:ContextualTermResult $target $trace-ab)
         (LangDef:ContextualTermResult $target $trace-ba)
         (LangDef:ContextualTermResult $target $trace-bb))
        (TPTPFofCnfExample.DuplicateDerivationsPreserved 4))
      ($_ (TPTPFofCnfExample.MultiplicityFailed $result)))))

(= (example:missing-serialization-row-summary $result)
   (case $result
     (((LangDef:ContextualTermResults LangDef:ContextComplete)
        (TPTPFofCnfExample.MissingClauseNameRejected))
      ($_ (TPTPFofCnfExample.MissingRowFailed $result)))))

!(let $official (example:official-truth)
   (let $named-result
        (tptp:official-fof->named-reducts 32 500000 $official)
     (let $named (example:only-target $named-result)
       (let $resolved-result
            (tptp:named-fof->resolved-reducts
              32 500000 (LangDef:RelationRows) $named)
         (let $resolved (example:only-target $resolved-result)
           (let $nnf-result
                (tptp:resolved-fof->positive-nnf-reducts
                  32 500000 $resolved)
             (let $nnf (example:only-target $nnf-result)
               (let $prenex-result
                    (tptp:nnf->prenex-reducts 32 500000 $nnf)
                 (let $prenex-receipt
                      (example:only-target $prenex-result)
                   (let $prenex (example:prenex-form $prenex-receipt)
                     (let $skolem-result
                          (tptp:prenex->skolem-reducts
                            32 500000
                            (tptp-fof-skolem-term:env-nil)
                            (tptp-fof-resolved:index-zero)
                            (tptp-fof-resolved:index-zero)
                            $prenex)
                       (let $skolem-receipt
                            (example:only-target $skolem-result)
                         (let $skolem
                              (example:skolem-form $skolem-receipt)
                           (let $naming-result
                                (tptp:skolem-fof->definitional-naming-reducts
                                  32 500000
                                  (tptp-fof-resolved:index-zero)
                                  (tptp-fof-resolved:index-zero)
                                  $skolem)
                             (let $naming-receipt
                                  (example:only-target $naming-result)
                               (let $named-output
                                    (example:named-output $naming-receipt)
                                 (let $cnf-result
                                      (tptp:definitional-naming->cnf-reducts
                                        32 500000 $named-output)
                                   (let $cnf-receipt
                                        (example:only-target $cnf-result)
                                     (let $cnf
                                          (example:cnf-output $cnf-receipt)
                                       (let $skolem-output
                                            (example:skolem-output
                                              $skolem-receipt)
                                         (let $batch-result
                                              (tptp:clausification-batch-reducts
                                                32 500000
                                                (tptp-fof-batch:occurrence
                                                  "truth-example" 0)
                                                (tptp-fof-batch:positive)
                                                $skolem-output
                                                $cnf)
                                           (let $batch
                                                (example:only-target
                                                  $batch-result)
                                             (let $allocation-result
                                                    (tptp:allocate-cnf-names-reducts
                                                      32 500000 $batch
                                                      (tptp-fof-resolved:index-zero))
                                                 (let $allocated
                                                      (example:only-target
                                                        $allocation-result)
                                                   (let $clause-name-row
                                                        (tptp:official-clause-name-row
                                                          7000
                                                          (PApp
                                                            "tptp-fof-resolved:index-zero"
                                                            LNil)
                                                          "truth_clause")
                                                     (let $_
                                                          (println!
                                                            (example:official-cnf-summary
                                                              (tptp:allocated-cnf->official-ast-reducts
                                                                32 500000
                                                                (LangDef:RelationRows
                                                                  $clause-name-row)
                                                                $allocated)))
                                                       (println!
                                                         (example:missing-serialization-row-summary
                                                           (tptp:allocated-cnf->official-ast-reducts
                                                             32 500000
                                                             (LangDef:RelationRows)
                                                             $allocated)))))))))))))))))))))))))))))

; Duplicate external decisions remain duplicate derivations.  lib_tptp does
; not turn the contextual relation into a first-result convention.
!(let $name-pattern
      (PApp "X" LNil)
   (let $equal-pattern
        (PApp "pattern-equality-decision:equal" LNil)
     (let $row-a
          (LangDef:RelationRow
            "PatternEqualityDecision" 7100
            $name-pattern $name-pattern $equal-pattern)
       (let $row-b
            (LangDef:RelationRow
              "PatternEqualityDecision" 7101
              $name-pattern $name-pattern $equal-pattern)
         (println!
           (example:duplicate-resolution-summary
             (tptp:named-fof->resolved-reducts
               32 500000 (LangDef:RelationRows $row-a $row-b)
               (tptp-fof-named:all
                 (tptp-fof-named:name "X")
                 (tptp-fof-named:equal
                   (tptp-fof-named:term-variable
                     (tptp-fof-named:name "X"))
                   (tptp-fof-named:term-variable
                     (tptp-fof-named:name "X")))))))))))
