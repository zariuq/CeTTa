; Generated from the authored Mettapedia rhoCalc LanguageDef.
; Concrete .rho/.mrho syntax and the CeTTa runtime ABI are separate data.
(gslt-presentation-v1 RhoAbstractSyntaxV1
  (signature
    (operator rho-language 1)
    (operator rho-sort 2)
    (operator rho-constructor 3)
    (operator rho-args 2)
    (operator rho-simple 1)
    (operator rho-abstraction 1)
    (operator rho-multi-abstraction 1)
    (operator rho-arrow 2)
    (operator rho-multi-binder 1)
    (operator rho-collection 2))
  (equations)
  (rewrites
    (rule rho-language-RhoCalc
      (head (rho-language RhoCalc))
      (body))
    (rule rho-sort-Proc
      (head (rho-sort Proc ast))
      (body))
    (rule rho-sort-Name
      (head (rho-sort Name ast))
      (body))
    (rule rho-constructor-PZero
      (head (rho-constructor PZero Proc rho-no-args))
      (body))
    (rule rho-constructor-PDrop
      (head (rho-constructor PDrop Proc (rho-args (rho-simple Name) rho-no-args)))
      (body))
    (rule rho-constructor-NQuote
      (head (rho-constructor NQuote Name (rho-args (rho-simple Proc) rho-no-args)))
      (body))
    (rule rho-constructor-PPar
      (head (rho-constructor PPar Proc (rho-args (rho-simple (rho-collection hash-bag Proc)) rho-no-args)))
      (body))
    (rule rho-constructor-POutput
      (head (rho-constructor POutput Proc (rho-args (rho-simple Name) (rho-args (rho-simple Proc) rho-no-args))))
      (body))
    (rule rho-constructor-PInput
      (head (rho-constructor PInput Proc (rho-args (rho-simple Name) (rho-args (rho-abstraction (rho-arrow Name Proc)) rho-no-args))))
      (body))
  ))
