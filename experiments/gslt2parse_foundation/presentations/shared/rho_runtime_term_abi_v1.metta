; CeTTa representation map for the abstract rho signature.
; This is an implementation ABI, not a second semantic language definition.
(gslt-presentation-v1 RhoRuntimeTermABIV1
  (signature
    (operator rho-runtime-abi 1)
    (operator rho-runtime-shape 5)
    (operator rho-runtime-collection-policy 4)
    (operator rho-runtime-projection-surface 10)
    (operator rho-runtime-projection-rule 3)
    (operator rho-projection-cons 2)
    (operator rho-projection-fixed 1)
    (operator rho-projection-quote 1)
    (operator rho-projection-binder 3))
  (equations)
  (rewrites
    (rule rho-runtime-abi-v1
      (head (rho-runtime-abi v1))
      (body))
    (rule rho-runtime-PZero
      (head (rho-runtime-shape PZero symbol rho:nil 0 none))
      (body))
    (rule rho-runtime-PDrop
      (head (rho-runtime-shape PDrop fixed rho:drop 1 none))
      (body))
    (rule rho-runtime-NQuote
      (head (rho-runtime-shape NQuote fixed rho:quote 1 none))
      (body))
    (rule rho-runtime-PPar
      (head (rho-runtime-shape PPar variadic rho:par variadic none))
      (body))
    (rule rho-runtime-POutput
      (head (rho-runtime-shape POutput fixed rho:send 2 none))
      (body))
    (rule rho-runtime-PInput
      (head (rho-runtime-shape PInput binder rho:recv 3 1))
      (body))
    (rule rho-runtime-PPar-collection
      (head (rho-runtime-collection-policy PPar hash-bag rho:par rho:nil))
      (body))

    (rule rho-runtime-projection-surface-rho
      (head
        (rho-runtime-projection-surface
          rho node document pair cons nil cp
          (rho-projection-cons identifier
            (rho-projection-cons variable
              (rho-projection-cons bare-name
                (rho-projection-cons binder rho-projection-nil))))
          (rho-projection-cons variable
            (rho-projection-cons bare-name rho-projection-nil))
          binder))
      (body))
    (rule rho-runtime-projection-surface-mrho
      (head
        (rho-runtime-projection-surface
          mrho node document pair cons nil cp
          (rho-projection-cons variable
            (rho-projection-cons binder rho-projection-nil))
          (rho-projection-cons variable rho-projection-nil)
          binder))
      (body))

    (rule rho-runtime-projection-rho-PZero
      (head (rho-runtime-projection-rule rho PZero rho-projection-symbol))
      (body))
    (rule rho-runtime-projection-rho-PDrop
      (head
        (rho-runtime-projection-rule rho PDrop
          (rho-projection-fixed
            (rho-projection-cons 0 rho-projection-nil))))
      (body))
    (rule rho-runtime-projection-rho-NQuote
      (head
        (rho-runtime-projection-rule rho NQuote
          (rho-projection-quote 0)))
      (body))
    (rule rho-runtime-projection-rho-PPar
      (head (rho-runtime-projection-rule rho PPar rho-projection-monoid))
      (body))
    (rule rho-runtime-projection-rho-POutput
      (head
        (rho-runtime-projection-rule rho POutput
          (rho-projection-fixed
            (rho-projection-cons 0
              (rho-projection-cons 1 rho-projection-nil)))))
      (body))
    (rule rho-runtime-projection-rho-PInput
      (head
        (rho-runtime-projection-rule rho PInput
          (rho-projection-binder 1 0 2)))
      (body))

    (rule rho-runtime-projection-mrho-PZero
      (head (rho-runtime-projection-rule mrho PZero rho-projection-symbol))
      (body))
    (rule rho-runtime-projection-mrho-PDrop
      (head
        (rho-runtime-projection-rule mrho PDrop
          (rho-projection-fixed
            (rho-projection-cons 0 rho-projection-nil))))
      (body))
    (rule rho-runtime-projection-mrho-NQuote
      (head
        (rho-runtime-projection-rule mrho NQuote
          (rho-projection-quote 0)))
      (body))
    (rule rho-runtime-projection-mrho-PPar
      (head (rho-runtime-projection-rule mrho PPar rho-projection-monoid))
      (body))
    (rule rho-runtime-projection-mrho-POutput
      (head
        (rho-runtime-projection-rule mrho POutput
          (rho-projection-fixed
            (rho-projection-cons 0
              (rho-projection-cons 1 rho-projection-nil)))))
      (body))
    (rule rho-runtime-projection-mrho-PInput
      (head
        (rho-runtime-projection-rule mrho PInput
          (rho-projection-binder 0 1 2)))
      (body))
  ))
