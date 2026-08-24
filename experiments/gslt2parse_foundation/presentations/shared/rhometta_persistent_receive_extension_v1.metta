; Rhometta target extension for a replicated rho input.
; Strict RhoCalc remains unchanged.  PPersistentInput denotes the derived
; process PReplicate(PInput channel (lambda body)); its runtime representation
; is the fused rho:recv-persistent form.
(gslt-presentation-v1 RhoMettaPersistentReceiveExtensionV1
  (signature
    (operator rhometta-language-extension 2)
    (operator rhometta-constructor-extension 3)
    (operator rhometta-runtime-shape-extension 5)
    (operator rhometta-rewrite-schema 3)
    (operator rho-pattern-apply 2)
    (operator rho-pattern-args 2)
    (operator rho-pattern-lambda 1)
    (operator rho-pattern-subst 2)
    (operator rho-pattern-collection 3))
  (equations)
  (rewrites
    (rule rhometta-persistent-receive-extension-v1
      (head
        (rhometta-language-extension
          RhoCalc RhoMettaPersistentReceiveV1))
      (body))
    (rule rhometta-constructor-PPersistentInput
      (head
        (rhometta-constructor-extension
          PPersistentInput Proc
          (rho-args
            (rho-simple Name)
            (rho-args
              (rho-abstraction (rho-arrow Name Proc))
              rho-no-args))))
      (body))
    (rule rhometta-runtime-PPersistentInput
      (head
        (rhometta-runtime-shape-extension
          PPersistentInput binder rho:recv-persistent 3 1))
      (body))

    ; PersistentComm keeps the input after communication.  The explicit
    ; substitution is the same quote-then-capture-avoiding substitution used
    ; by strict rho COMM; only the input lifetime differs.
    (rule rhometta-rewrite-PersistentComm
      (head
        (rhometta-rewrite-schema
          PersistentComm
          (rho-pattern-collection hash-bag
            (rho-pattern-args
              (rho-pattern-apply POutput
                (rho-pattern-args ?channel
                  (rho-pattern-args ?payload rho-pattern-no-args)))
              (rho-pattern-args
                (rho-pattern-apply PPersistentInput
                  (rho-pattern-args ?channel
                    (rho-pattern-args
                      (rho-pattern-lambda ?body)
                      rho-pattern-no-args)))
                rho-pattern-no-args))
            ?rest)
          (rho-pattern-collection hash-bag
            (rho-pattern-args
              (rho-pattern-apply PPersistentInput
                (rho-pattern-args ?channel
                  (rho-pattern-args
                    (rho-pattern-lambda ?body)
                    rho-pattern-no-args)))
              (rho-pattern-args
                (rho-pattern-subst ?body
                  (rho-pattern-apply NQuote
                    (rho-pattern-args ?payload rho-pattern-no-args)))
                rho-pattern-no-args))
            ?rest)))
      (body))
  ))
