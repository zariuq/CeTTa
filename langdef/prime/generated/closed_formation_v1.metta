(gslt-presentation-v1 prime-closed-formation-v1
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber prime)
    (outcome-algebra (PEstablished PRefuted PUndetermined PIncomplete)
      (exclusive)
      (default PUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default))
  (signature
    (operator PCons 2)
    (operator PArrow 1)
    (operator PrimeForm 1)
    (operator PrimeFormComponents 1)
    (operator PrimeNotForm 1)
  )
  (equations)
  (rewrites
    (rule prime-form-type
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PType)) (body))
    (rule prime-form-dynamic
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PDynamic)) (body))
    (rule prime-form-atom
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PAtom)) (body))
    (rule prime-form-symbol
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PSymbol)) (body))
    (rule prime-form-variable
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PVariable)) (body))
    (rule prime-form-expression
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PExpression)) (body))
    (rule prime-form-grounded
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PGrounded)) (body))
    (rule prime-form-number
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PNumber)) (body))
    (rule prime-form-bool
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PBool)) (body))
    (rule prime-form-string
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PString)) (body))
    (rule prime-form-error
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm PError)) (body))
    (rule prime-form-components-one
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeFormComponents (PCons ?t PNil))) (body (PrimeForm ?t)))
    (rule prime-form-components-cons
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeFormComponents (PCons ?t ?ts))) (body (PrimeForm ?t) (PrimeFormComponents ?ts)))
    (rule prime-form-arrow
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus)) (head (PrimeForm (PArrow ?ts))) (body (PrimeFormComponents ?ts)))
    (rule prime-not-form-empty-arrow
      (nik-rule-frame-v1
        (native-projection native-formation-refutation)
        (role calculus)) (head (PrimeNotForm (PArrow PNil))) (body))
  ))
