!(import! &self lib_tptp)

(= (test:missing-finish-result
      (LangDef:ContextualTermResults LangDef:ContextComplete
        (LangDef:ContextualTermResult
          (dwm:halted
            (dcm:outcome-fault (dcm:fault-missing-finish))
            (dcm:nodes-nil))
          (LangDef:RuleTrace 1 "dwm:lift:dcm:missing-finish"))))
   True)

(= (test:malformed-record-result
      (LangDef:ContextualTermResults LangDef:ContextComplete
        (LangDef:ContextualTermResult
          (dwm:halted
            (dcm:outcome-fault (dcm:fault-malformed-record))
            (dcm:nodes-nil))
          (LangDef:RuleTrace 0 "dwm:malformed-record"
            (LangDef:RelationEvidence
              0 LangDef:ExternalRelation 0 9100)))))
   True)

(= (test:complete-without-reducts
      (LangDef:ContextualTermResults LangDef:ContextComplete))
   True)

(= (test:run)
   (let $word-machine
        (langdef:load-language
          "langdef/logic/derivation_word_machine_v1.metta")
     (let $empty-start
          (dwm:run
            (dwm:records-nil)
            (dcm:nodes-nil)
            (dcm:index 0)
            (dcm:root-none)
            (dcm:service-state-initial))
       (let $empty-result
            (langdef:rewrite-term-at
              $word-machine Config Config 1 200000
              (LangDef:RelationRows) $empty-start)
         (let $halted-result
              (langdef:rewrite-term-at
                $word-machine Config Config 1 200000
                (LangDef:RelationRows)
                (dwm:halted
                  (dcm:outcome-fault (dcm:fault-missing-finish))
                  (dcm:nodes-nil)))
           (let $malformed-start
                (dwm:run
                  (dwm:records-cons
                    (dwm:words-nil)
                    (dwm:records-nil))
                  (dcm:nodes-nil)
                  (dcm:index 0)
                  (dcm:root-none)
                  (dcm:service-state-initial))
             (let $decode-rejected-row
                  (LangDef:RelationRow "DWMDecodeRecord" 9100
                    (PApp "dwm:words-nil" LNil)
                    (PApp "dwm:decode-rejected" LNil))
               (let $malformed-result
                    (langdef:rewrite-term-at
                      $word-machine Config Config 1 200000
                      (LangDef:RelationRows $decode-rejected-row)
                      $malformed-start)
                 (let $missing-decision-result
                      (langdef:rewrite-term-at
                        $word-machine Config Config 1 200000
                        (LangDef:RelationRows) $malformed-start)
                   (case
                     ((test:missing-finish-result $empty-result)
                      (test:complete-without-reducts $halted-result)
                      (test:malformed-record-result $malformed-result)
                      (test:complete-without-reducts
                        $missing-decision-result))
                     (((True True True True)
                        (case (langdef:close-language $word-machine)
                          ((True
                             (DerivationWordMachineV1Summary 5 5 0))
                           ($_
                             (DerivationWordMachineV1CloseMismatch)))))
                      ($_
                        (let $_closed
                             (langdef:close-language $word-machine)
                          (DerivationWordMachineV1ObservationMismatch))))))))))))))

!(println! (test:run))
