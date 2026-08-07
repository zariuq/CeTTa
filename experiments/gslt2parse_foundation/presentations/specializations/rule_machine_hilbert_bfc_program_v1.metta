; RuleMachineProgram's bounded Hilbert specialization, expressed as a compiler from the
; generic RuleMachineCoreV1 block IR to an explicit instruction program.

(gslt-presentation-v1 HilbertBFCProgramSpecializationV1
  (signature
    (operator imp 2)
    (operator compile-rule-program-block 2)
    (operator compile-rule-program-source 3)
    (operator rmbc-fresh 1)
    (operator rmbc-hyp-add 1)
    (operator rmbc-instantiate-template 2)
    (operator rmbc-make-fun 3)
    (operator rmbc-make-imp 3)
    (operator rmbc-proof-apply 1)
    (operator rmbc-proof-wrap 1)
    (operator rmbc-require-fun 2)
    (operator rmbc-set-type 1)
    (operator rmbc-unify 2)
    (operator rule-program-block 4)
    (operator rule-program-code 1)
    (operator rule-program-cons 2)
    (operator rule-program-template 1))

  (equations)

  (rewrites
    (rule compile-rule-program-axiom
      (head
        (compile-rule-program-block
          (bc-block
            ?id ?source (bc-match ?schema) (bc-goals rm-nil)
            (bc-build (rm-proof-atom ?proof)) bc-emit)
          (rule-program-block
            ?id ?source (rule-program-template ?schema)
            (rule-program-code
              (rule-program-cons (rmbc-require-fun r0 r1)
              (rule-program-cons (rmbc-instantiate-template r2 template0)
              (rule-program-cons (rmbc-unify r0 r2)
              (rule-program-cons (rmbc-set-type r1)
              (rule-program-cons (rmbc-proof-apply ?proof)
              (rule-program-cons (rmbc-hyp-add 0)
              (rule-program-cons rmbc-emit rule-program-nil)))))))))))
      (body))

    (rule compile-rule-program-inverse-mp
      (head
        (compile-rule-program-block
          (bc-block
            ?id ?source (bc-match ?conclusion)
            (bc-goals
              (rm-cons (rm-premise ?left ?antecedent)
                (rm-cons
                  (rm-premise ?right (imp ?antecedent ?conclusion))
                  rm-nil)))
            (bc-build
              (rm-proof-app ?proof
                (rm-cons ?left (rm-cons ?right rm-nil))))
            bc-emit)
          (rule-program-block
            ?id ?source (rule-program-template rule-program-none)
            (rule-program-code
              (rule-program-cons (rmbc-require-fun r0 r1)
              (rule-program-cons (rmbc-fresh r2)
              (rule-program-cons (rmbc-make-imp r3 r2 r0)
              (rule-program-cons (rmbc-make-fun r4 r2 r1)
              (rule-program-cons (rmbc-make-fun r5 r3 r4)
              (rule-program-cons (rmbc-set-type r5)
              (rule-program-cons (rmbc-proof-wrap ?proof)
              (rule-program-cons (rmbc-hyp-add 2)
              (rule-program-cons rmbc-emit rule-program-nil)))))))))))))
      (body))

    (rule compile-rule-program-source-block
      (head (compile-rule-program-source ?guest ?id ?program))
      (body
        (source-block-id ?guest ?id ?source)
        (compile-block ?source ?bytecode)
        (compile-rule-program-block ?bytecode ?program)))
  ))
