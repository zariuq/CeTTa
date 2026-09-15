; Public lower-level execution example for a theorem-derived verification-word
; artifact.  The operational result is intentionally not named tptp:verify:
; semantic promotion additionally requires an authenticated official-DAG
; compiler, calculus service, and composition theorem.

!(import! &self lib_tptp)
!(import! &self ../../langdef/tptp/generated/official_ground_resolution_word_canary_v1.metta)

(= (example:trace-count (TPTP:VerificationWordTracesNilV1)) 0)

(= (example:trace-count
      (TPTP:VerificationWordTracesConsV1 $_trace $rest))
   (+ 1 (example:trace-count $rest)))

(= (example:verified
      $start
      (TPTP:VerificationWordExecutionV1 $_wire $start
        (TPTP:VerificationWordHaltedV1
          (dcm:outcome-verified $_id $_formula $_obligation)
          $_nodes $traces 7)))
   (== (example:trace-count $traces) 7))

(= (example:step-fuel-exhausted
      $start
      (TPTP:VerificationWordExecutionV1 $_wire $start
        (TPTP:VerificationWordResourceExhaustedV1
          step-fuel $start
          (TPTP:VerificationWordTracesNilV1) 0)))
   True)

(= (example:missing-service-incomplete
      $start
      (TPTP:VerificationWordExecutionV1 $_wire $start
        (TPTP:VerificationWordIncompleteV1
          no-licensed-transition $start
          (TPTP:VerificationWordTracesNilV1) 0)))
   True)

(= (example:terminal-needs-no-replay
      $terminal
      (TPTP:VerificationWordExecutionV1 $_wire $terminal
        (TPTP:VerificationWordHaltedV1
          $_outcome $_nodes
          (TPTP:VerificationWordTracesNilV1) 0)))
   True)

(= (example:malformed-start-rejected
      (TPTP:VerificationWordExecutionV1 $_wire (not-a-config)
        (TPTP:VerificationWordStartRejectedV1
          $_reason $_message)))
   True)

(= (example:ambiguity-fails-closed
      $start
      (TPTP:VerificationWordExecutionV1 $_wire $start
        (TPTP:VerificationWordExecutionFaultV1
          non-singleton-or-runtime-fault
          (LangDef:ContextualTermResults LangDef:ContextComplete
            (LangDef:ContextualTermResult $_target-0 $_trace-0)
            (LangDef:ContextualTermResult $_target-1 $_trace-1))
          $start (TPTP:VerificationWordTracesNilV1) 0)))
   True)

(= (example:summary $verified $fuel $incomplete $terminal $rejected $ambiguous)
   (case ($verified $fuel $incomplete $terminal $rejected $ambiguous)
     (((True True True True True True)
        (TPTPVerificationWordExecutionV1Summary 6 6 0))
      ($_
        (TPTPVerificationWordExecutionV1SummaryFailed)))))

(= (example:run)
   (let $start (tptp-ground-word-canary:start)
     (let $expected (tptp-ground-word-canary:expected)
       (let $rows (tptp-ground-word-canary:relation-rows)
         (let $malformed-start
              (dwm:run
                (dwm:records-cons
                  (dwm:words-nil)
                  (dwm:records-nil))
                (dcm:nodes-nil)
                (dcm:index 0)
                (dcm:root-none)
                (dcm:service-state-initial))
           (let $decode-rejected-0
                (LangDef:RelationRow "DWMDecodeRecord" 9300
                  (PApp "dwm:words-nil" LNil)
                  (PApp "dwm:decode-rejected" LNil))
             (let $decode-rejected-1
                  (LangDef:RelationRow "DWMDecodeRecord" 9301
                    (PApp "dwm:words-nil" LNil)
                    (PApp "dwm:decode-rejected" LNil))
               (example:summary
                 (example:verified $start
                   (tptp:execute-verification-word
                     7 200000 $rows $start))
                 (example:step-fuel-exhausted $start
                   (tptp:execute-verification-word
                     0 200000 $rows $start))
                 (example:missing-service-incomplete $start
                   (tptp:execute-verification-word
                     7 200000 (LangDef:RelationRows) $start))
                 (example:terminal-needs-no-replay $expected
                   (tptp:execute-verification-word
                     0 200000 $rows $expected))
                 (example:malformed-start-rejected
                   (tptp:execute-verification-word
                     1 200000 (LangDef:RelationRows) (not-a-config)))
                 (example:ambiguity-fails-closed $malformed-start
                   (tptp:execute-verification-word
                     1 200000
                     (LangDef:RelationRows
                       $decode-rejected-0 $decode-rejected-1)
                     $malformed-start))))))))))

!(println! (example:run))
