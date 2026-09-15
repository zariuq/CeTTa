; Execute the exact finite word artifact projected from the theorem-checked
; official ground-resolution canary.  This demonstrates generic derivation-DAG
; bookkeeping with an explicit calculus-service table; it is not proof search
; and it is not a claim of general TSTP rule coverage.

!(import! &self lib_tptp)
!(import! &self ../../langdef/tptp/generated/official_ground_resolution_word_canary_v1.metta)
!(import! &self support/derivation_word_executor.metta)

(= (word-check:trace-evidence-count
      (LangDef:RuleTrace $_occurrence $_rule
        $_evidence-0 $_evidence-1))
   2)

(= (word-check:trace-evidence-count
      (LangDef:RuleTrace $_occurrence $_rule
        $_evidence-0 $_evidence-1 $_evidence-2))
   3)

(= (word-check:trace-evidence-count
      (LangDef:RuleTrace $_occurrence $_rule
        $_evidence-0 $_evidence-1 $_evidence-2 $_evidence-3))
   4)

(= (word-check:trace-evidence-count
      (LangDef:RuleTrace $_occurrence $_rule
        $_evidence-0 $_evidence-1 $_evidence-2 $_evidence-3
        $_evidence-4))
   5)

(= (word-check:traces-evidence-count (dwm-example:traces-nil)) 0)

(= (word-check:traces-evidence-count
      (dwm-example:traces-cons $trace $rest))
   (+ (word-check:trace-evidence-count $trace)
      (word-check:traces-evidence-count $rest)))

; The successful trace must consume the exact provider receipts projected
; from Lean, in the exact premise and instruction order.  The executor stores
; traces newest-first, so the pattern starts with the finish instruction.
(= (word-check:expected-receipts $traces)
   (case $traces
     (((dwm-example:traces-cons
         (LangDef:RuleTrace 20 "dwm:lift:dcm:finish-verified"
           (LangDef:RelationEvidence 0 LangDef:ExternalRelation 6 920024)
           (LangDef:RelationEvidence 1 LangDef:ExternalRelation 0 920025)
           (LangDef:RelationEvidence 2 LangDef:ExternalRelation 0 920026))
         (dwm-example:traces-cons
           (LangDef:RuleTrace 15 "dwm:lift:dcm:root-accept"
             (LangDef:RelationEvidence 0 LangDef:ExternalRelation 5 920022)
             (LangDef:RelationEvidence 1 LangDef:ExternalRelation 0 920023))
           (dwm-example:traces-cons
             (LangDef:RuleTrace 10 "dwm:lift:dcm:infer-accept"
               (LangDef:RelationEvidence 0 LangDef:ExternalRelation 4 920017)
               (LangDef:RelationEvidence 1 LangDef:ExternalRelation 4 920018)
               (LangDef:RelationEvidence 2 LangDef:ExternalRelation 4 920019)
               (LangDef:RelationEvidence 3 LangDef:ExternalRelation 1 920020)
               (LangDef:RelationEvidence 4 LangDef:ExternalRelation 1 920021))
             (dwm-example:traces-cons
               (LangDef:RuleTrace 10 "dwm:lift:dcm:infer-accept"
                 (LangDef:RelationEvidence 0 LangDef:ExternalRelation 3 920012)
                 (LangDef:RelationEvidence 1 LangDef:ExternalRelation 3 920013)
                 (LangDef:RelationEvidence 2 LangDef:ExternalRelation 3 920014)
                 (LangDef:RelationEvidence 3 LangDef:ExternalRelation 0 920015)
                 (LangDef:RelationEvidence 4 LangDef:ExternalRelation 0 920016))
               (dwm-example:traces-cons
                 (LangDef:RuleTrace 5 "dwm:lift:dcm:input-accept"
                   (LangDef:RelationEvidence 0 LangDef:ExternalRelation 2 920008)
                   (LangDef:RelationEvidence 1 LangDef:ExternalRelation 2 920009)
                   (LangDef:RelationEvidence 2 LangDef:ExternalRelation 2 920010)
                   (LangDef:RelationEvidence 3 LangDef:ExternalRelation 2 920011))
                 (dwm-example:traces-cons
                   (LangDef:RuleTrace 5 "dwm:lift:dcm:input-accept"
                     (LangDef:RelationEvidence 0 LangDef:ExternalRelation 1 920004)
                     (LangDef:RelationEvidence 1 LangDef:ExternalRelation 1 920005)
                     (LangDef:RelationEvidence 2 LangDef:ExternalRelation 1 920006)
                     (LangDef:RelationEvidence 3 LangDef:ExternalRelation 1 920007))
                   (dwm-example:traces-cons
                     (LangDef:RuleTrace 5 "dwm:lift:dcm:input-accept"
                       (LangDef:RelationEvidence 0 LangDef:ExternalRelation 0 920000)
                       (LangDef:RelationEvidence 1 LangDef:ExternalRelation 0 920001)
                       (LangDef:RelationEvidence 2 LangDef:ExternalRelation 0 920002)
                       (LangDef:RelationEvidence 3 LangDef:ExternalRelation 0 920003))
                     (dwm-example:traces-nil))))))))
        True)
      ($_ False))))

(= (word-check:mutate-first-receipt
      (dwm-example:traces-cons
        (LangDef:RuleTrace $occurrence $rule
          (LangDef:RelationEvidence
            $premise $source $tuple $_receipt)
          $evidence-1 $evidence-2)
        $rest))
   (dwm-example:traces-cons
     (LangDef:RuleTrace $occurrence $rule
       (LangDef:RelationEvidence
         $premise $source $tuple 999999)
       $evidence-1 $evidence-2)
     $rest))

(= (word-check:expected-run
      $expected (dwm-example:complete $expected $traces 7))
   (case
     ((word-check:expected-receipts $traces)
      (word-check:traces-evidence-count $traces)
      (word-check:expected-receipts
        (word-check:mutate-first-receipt $traces)))
     (((True 27 False) True)
      ($_ False))))

(= (word-check:empty-reducts
      (LangDef:ContextualTermResults LangDef:ContextComplete))
   True)

(= (word-check:admitted (LangDef:TermAdmitted)) True)

(= (word-check:six-step-exhaustion $result)
   (case $result
     (((dwm-example:fuel-exhausted
         (dwm:run $_records $_nodes $_next-index $_root $_service-state)
         $_traces 6)
        True)
      ($_ False))))

(= (word-check:missing-service-stuck $start $result)
   (case $result
     (((dwm-example:stuck
         $start (dwm-example:traces-nil) 0)
        True)
      ($_ False))))

(= (word-check:terminal-needs-no-replay $expected $result)
   (case $result
     (((dwm-example:complete
         $expected (dwm-example:traces-nil) 0)
        True)
      ($_ False))))

(= (word-check:run)
   (let $language
        (langdef:load-language
          "langdef/logic/derivation_word_machine_v1.metta")
     (let $start (tptp-ground-word-canary:start)
       (let $expected (tptp-ground-word-canary:expected)
         (let $rows (tptp-ground-word-canary:relation-rows)
             (let $execution
                  (dwm-example:execute $language $rows 7 $start)
             (let $irreducible
                  (langdef:rewrite-term-at
                    $language Config Config 1 200000 $rows $expected)
               (let $short-execution
                    (dwm-example:execute $language $rows 6 $start)
                 (let $without-service
                      (dwm-example:execute
                        $language (LangDef:RelationRows) 7 $start)
                   (let $terminal-execution
                        (dwm-example:execute $language $rows 0 $expected)
                     (let $start-admission
                      (langdef:admit-term $language Config $start)
                       (let $target-admission
                            (langdef:admit-term $language Config $expected)
                         (case
                           ((word-check:expected-run $expected $execution)
                            (word-check:empty-reducts $irreducible)
                            (word-check:six-step-exhaustion $short-execution)
                            (word-check:missing-service-stuck
                              $start $without-service)
                            (word-check:terminal-needs-no-replay
                              $expected $terminal-execution)
                            (word-check:admitted $start-admission)
                            (word-check:admitted $target-admission)
                            (tptp-ground-word-canary:step-count)
                            (tptp-ground-word-canary:relation-row-count))
                           (((True True True True True True True 7 27)
                              (case (langdef:close-language $language)
                                ((True
                                   (TPTPGroundResolutionWordCheck
                                     7 27 VerifiedTarget
                                     SinglePassFuelBoundedExecution
                                     ServiceEvidenceConsumedAndLoadBearing))
                                 ($_
                                   (TPTPGroundResolutionWordCheckCloseFault)))))
                            ($_
                              (let $_closed
                                   (langdef:close-language $language)
                                (TPTPGroundResolutionWordCheckFailed)))))))))))))))))

!(println! (word-check:run))
