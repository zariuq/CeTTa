!(import! &self lib_tptp)
!(import! &self ../../../examples/tptp/support/derivation_word_executor.metta)

(= (test:complete-after-one
      (dwm-example:complete
        (dwm:halted
          (dcm:outcome-fault (dcm:fault-missing-finish))
          (dcm:nodes-nil))
        (dwm-example:traces-cons
          (LangDef:RuleTrace 1 "dwm:lift:dcm:missing-finish")
          (dwm-example:traces-nil))
        1))
   True)

(= (test:fuel-exhausted-at-start
      (dwm-example:fuel-exhausted
        (dwm:run
          (dwm:records-nil)
          (dcm:nodes-nil)
          (dcm:index 0)
          (dcm:root-none)
          (dcm:service-state-initial))
        (dwm-example:traces-nil)
        0))
   True)

(= (test:already-halted-with-zero-fuel
      (dwm-example:complete
        (dwm:halted
          (dcm:outcome-fault (dcm:fault-missing-finish))
          (dcm:nodes-nil))
        (dwm-example:traces-nil)
        0))
   True)

(= (test:stuck-without-a-decode-row
      (dwm-example:stuck
        (dwm:run
          (dwm:records-cons
            (dwm:words-nil)
            (dwm:records-nil))
          (dcm:nodes-nil)
          (dcm:index 0)
          (dcm:root-none)
          (dcm:service-state-initial))
        (dwm-example:traces-nil)
        0))
   True)

(= (test:ambiguous-transition-is-a-fault
      (dwm-example:transition-fault
        (LangDef:ContextualTermResults LangDef:ContextComplete
          (LangDef:ContextualTermResult $_target-0 $_trace-0)
          (LangDef:ContextualTermResult $_target-1 $_trace-1))
        (dwm:run
          (dwm:records-cons
            (dwm:words-nil)
            (dwm:records-nil))
          (dcm:nodes-nil)
          (dcm:index 0)
          (dcm:root-none)
          (dcm:service-state-initial))
        (dwm-example:traces-nil)
        0))
   True)

(= (test:summary $complete $fuel $halted $stuck $ambiguous)
   (case ($complete $fuel $halted $stuck $ambiguous)
     (((True True True True True)
        (DerivationWordExecutorV1Summary 5 5 0))
      ($_
        (DerivationWordExecutorV1SummaryFailed)))))

(= (test:run)
   (let $language
        (langdef:load-language
          "langdef/logic/derivation_word_machine_v1.metta")
     (let $empty-start
          (dwm:run
            (dwm:records-nil)
            (dcm:nodes-nil)
            (dcm:index 0)
            (dcm:root-none)
            (dcm:service-state-initial))
       (let $halted
            (dwm:halted
              (dcm:outcome-fault (dcm:fault-missing-finish))
              (dcm:nodes-nil))
         (let $malformed-start
              (dwm:run
                (dwm:records-cons
                  (dwm:words-nil)
                  (dwm:records-nil))
                (dcm:nodes-nil)
                (dcm:index 0)
                (dcm:root-none)
                (dcm:service-state-initial))
           (let $complete
                (dwm-example:execute
                  $language (LangDef:RelationRows) 1 $empty-start)
             (let $fuel
                  (dwm-example:execute
                    $language (LangDef:RelationRows) 0 $empty-start)
               (let $already-halted
                    (dwm-example:execute
                      $language (LangDef:RelationRows) 0 $halted)
                 (let $stuck
                      (dwm-example:execute
                        $language (LangDef:RelationRows) 1
                        $malformed-start)
                   (let $decode-rejected-0
                        (LangDef:RelationRow "DWMDecodeRecord" 9100
                          (PApp "dwm:words-nil" LNil)
                          (PApp "dwm:decode-rejected" LNil))
                     (let $decode-rejected-1
                          (LangDef:RelationRow "DWMDecodeRecord" 9101
                            (PApp "dwm:words-nil" LNil)
                            (PApp "dwm:decode-rejected" LNil))
                       (let $ambiguous
                            (dwm-example:execute
                              $language
                              (LangDef:RelationRows
                                $decode-rejected-0 $decode-rejected-1)
                              1 $malformed-start)
                         (let $summary
                              (test:summary
                                (test:complete-after-one $complete)
                                (test:fuel-exhausted-at-start $fuel)
                                (test:already-halted-with-zero-fuel
                                  $already-halted)
                                (test:stuck-without-a-decode-row $stuck)
                                (test:ambiguous-transition-is-a-fault
                                  $ambiguous))
                           (let $_closed
                                (langdef:close-language $language)
                             $summary))))))))))))))

!(println! (test:run))
