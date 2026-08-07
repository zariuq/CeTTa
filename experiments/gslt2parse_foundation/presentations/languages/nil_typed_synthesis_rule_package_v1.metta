; The fact and rule package used by experimental/synthesis/SynthesizeTest.metta.

(gslt-presentation-v1 NilTypedSynthesisRulePackageV1
  (signature
    (operator arrow 2)
    (operator arrow 3))
  (equations)
  (rewrites
    (rule nil-synthesis-f
      (head
        (source-block nil-typed-synthesis
          (rm-block f SynthesizeTest.metta:f
            (rm-proof-atom f) rm-nil (arrow Number String))))
      (body))
    (rule nil-synthesis-g
      (head
        (source-block nil-typed-synthesis
          (rm-block g SynthesizeTest.metta:g
            (rm-proof-atom g) rm-nil (arrow String Bool))))
      (body))
    (rule nil-synthesis-h
      (head
        (source-block nil-typed-synthesis
          (rm-block h SynthesizeTest.metta:h
            (rm-proof-atom h) rm-nil (arrow Bool Number))))
      (body))
    (rule nil-synthesis-i
      (head
        (source-block nil-typed-synthesis
          (rm-block i SynthesizeTest.metta:i
            (rm-proof-atom i) rm-nil (arrow String Number Bool))))
      (body))
    (rule nil-synthesis-compose
      (head
        (source-block nil-typed-synthesis
          (rm-block compose SynthesizeTest.metta:composition
            (rm-proof-app compose
              (rm-cons ?outer-proof (rm-cons ?inner-proof rm-nil)))
            (rm-cons
              (rm-premise ?outer-proof (arrow ?middle ?output))
              (rm-cons
                (rm-premise ?inner-proof (arrow ?input ?middle))
                rm-nil))
            (arrow ?input ?output))))
      (body))
    (rule nil-synthesis-blackbird
      (head
        (source-block nil-typed-synthesis
          (rm-block blackbird SynthesizeTest.metta:blackbird
            (rm-proof-app blackbird
              (rm-cons ?outer-proof (rm-cons ?inner-proof rm-nil)))
            (rm-cons
              (rm-premise ?outer-proof (arrow ?middle ?output))
              (rm-cons
                (rm-premise ?inner-proof (arrow ?input1 ?input2 ?middle))
                rm-nil))
            (arrow ?input1 ?input2 ?output))))
      (body))
    ; r0 intentionally omits Blackbird.  Publishing that one source block
    ; yields r1 while every r0 artifact remains an immutable old snapshot.
    (rule nil-synthesis-r0
      (head
        (guest-revision nil-typed-synthesis r0
          (artifact-link
            (artifact-link
              (artifact-link
                (artifact-link
                  (artifact-link
                    (artifact-empty nil-typed-synthesis)
                    (block-ref nil-typed-synthesis f))
                  (block-ref nil-typed-synthesis g))
                (block-ref nil-typed-synthesis h))
              (block-ref nil-typed-synthesis i))
            (block-ref nil-typed-synthesis compose))))
      (body))
    (rule nil-synthesis-r1
      (head
        (guest-revision nil-typed-synthesis r1
          (artifact-link ?r0
            (block-ref nil-typed-synthesis blackbird))))
      (body
        (guest-revision nil-typed-synthesis r0 ?r0)))
  ))
