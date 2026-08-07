; One bounded rule package, compiled and run through the native realization of
; RuleMachineCoreV1.  r1 links only Blackbird; r0 remains an old snapshot.

(= (synth-r0)
  (compile:rule-package r0
    (rm-package
      (rm-block f SynthesizeTest.metta:f
        (rm-proof-atom f) rm-nil (arrow Number String))
      (rm-block g SynthesizeTest.metta:g
        (rm-proof-atom g) rm-nil (arrow String Bool))
      (rm-block h SynthesizeTest.metta:h
        (rm-proof-atom h) rm-nil (arrow Bool Number))
      (rm-block i SynthesizeTest.metta:i
        (rm-proof-atom i) rm-nil (arrow String Number Bool))
      (rm-block compose SynthesizeTest.metta:composition
        (rm-proof-app compose
          (rm-cons $outer-proof (rm-cons $inner-proof rm-nil)))
        (rm-cons
          (rm-premise $outer-proof (arrow $middle $output))
          (rm-cons
            (rm-premise $inner-proof (arrow $input $middle))
            rm-nil))
        (arrow $input $output)))))

(= (blackbird-block)
  (rm-block blackbird SynthesizeTest.metta:blackbird
    (rm-proof-app blackbird
      (rm-cons $outer-proof (rm-cons $inner-proof rm-nil)))
    (rm-cons
      (rm-premise $outer-proof (arrow $middle $output))
      (rm-cons
        (rm-premise $inner-proof (arrow $input1 $input2 $middle))
        rm-nil))
    (arrow $input1 $input2 $output)))

(= (synth-r1)
  (compile:link-rule (synth-r0) r1 (blackbird-block)))

!(compile:artifact-info (synth-r0))
!(compile:artifact-info (synth-r1))

; Negative and positive dynamic-publication witnesses.
!(compile:run (synth-r0) 2 10000 100 (arrow String Number Number))
!(compile:run (synth-r1) 2 10000 100 (arrow String Number Number))

; The pinned old artifact is unchanged after r1 exists.
!(compile:run (synth-r0) 2 10000 100 (arrow String Number Number))

; A separate composition rule uses the same bytecode machine.
!(compile:run (synth-r0) 2 10000 100 (arrow Number Bool))

; Fail-closed malformed input example.
!(compile:rule-package broken (rm-package (not-a-block x)))
