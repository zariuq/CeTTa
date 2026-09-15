(gslt-presentation-v1 ClauseDataToResolutionInputV1
  (signature
    (operator metta-equation 2)

    (operator fo-resolution:project-source-digest 1)
    (operator fo-resolution:project-occurrence 1)
    (operator fo-resolution:project-formula-name 1)
    (operator fo-resolution:project-role 1)
    (operator fo-resolution:project-symbol-kind 1)
    (operator fo-resolution:project-symbol-name 1)
    (operator fo-resolution:project-variable-name 1)
    (operator fo-resolution:project-variable-id 1)
    (operator fo-resolution:project-numeric-kind 1)
    (operator fo-resolution:project-term 1)
    (operator fo-resolution:project-terms 1)
    (operator fo-resolution:project-atom 1)
    (operator fo-resolution:project-literal 1)
    (operator fo-resolution:project-literals 1)
    (operator fo-resolution:project-clause 1)
    (operator fo-resolution:project-clauses 1)
    (operator fo-resolution:project-problem 1)

    (operator fo-cnf:source-digest 1)
    (operator fo-cnf:occurrence 2)
    (operator fo-cnf:formula-name-atomic 1)
    (operator fo-cnf:formula-name-integer 1)
    (operator fo-cnf:role 2)
    (operator fo-cnf:symbol-lower 0)
    (operator fo-cnf:symbol-quoted 0)
    (operator fo-cnf:symbol-defined 0)
    (operator fo-cnf:symbol-system 0)
    (operator fo-cnf:symbol-name 2)
    (operator fo-cnf:variable-name 1)
    (operator fo-cnf:variable-id 2)
    (operator fo-cnf:number-integer 0)
    (operator fo-cnf:number-rational 0)
    (operator fo-cnf:number-real 0)
    (operator fo-cnf:term-variable 1)
    (operator fo-cnf:term-function 2)
    (operator fo-cnf:term-number 2)
    (operator fo-cnf:term-distinct 1)
    (operator fo-cnf:terms-nil 0)
    (operator fo-cnf:terms-cons 2)
    (operator fo-cnf:atom-predicate 2)
    (operator fo-cnf:atom-equality 2)
    (operator fo-cnf:literal-positive 1)
    (operator fo-cnf:literal-negative 1)
    (operator fo-cnf:literals-nil 0)
    (operator fo-cnf:literals-cons 2)
    (operator fo-cnf:clause 6)
    (operator fo-cnf:clauses-nil 0)
    (operator fo-cnf:clauses-cons 2)
    (operator fo-cnf:problem 2)

    (operator fo-resolution:source-digest 1)
    (operator fo-resolution:occurrence 2)
    (operator fo-resolution:formula-name-atomic 1)
    (operator fo-resolution:formula-name-integer 1)
    (operator fo-resolution:role 1)
    (operator fo-resolution:symbol-lower 0)
    (operator fo-resolution:symbol-quoted 0)
    (operator fo-resolution:symbol-defined 0)
    (operator fo-resolution:symbol-system 0)
    (operator fo-resolution:symbol-name 2)
    (operator fo-resolution:variable-name 1)
    (operator fo-resolution:variable-id 2)
    (operator fo-resolution:number-integer 0)
    (operator fo-resolution:number-rational 0)
    (operator fo-resolution:number-real 0)
    (operator fo-resolution:term-variable 1)
    (operator fo-resolution:term-function 2)
    (operator fo-resolution:term-number 2)
    (operator fo-resolution:term-distinct 1)
    (operator fo-resolution:terms-nil 0)
    (operator fo-resolution:terms-cons 2)
    (operator fo-resolution:atom-predicate 2)
    (operator fo-resolution:atom-equality 2)
    (operator fo-resolution:literal-positive 1)
    (operator fo-resolution:literal-negative 1)
    (operator fo-resolution:literals-nil 0)
    (operator fo-resolution:literals-cons 2)
    (operator fo-resolution:clause 4)
    (operator fo-resolution:clauses-nil 0)
    (operator fo-resolution:clauses-cons 2)
    (operator fo-resolution:problem 2))
  (equations)
  (rewrites
    (rule project-source-digest
      (head
        (metta-equation
          (fo-resolution:project-source-digest
            (fo-cnf:source-digest ?value))
          (fo-resolution:source-digest ?value)))
      (body))

    (rule project-occurrence
      (head
        (metta-equation
          (fo-resolution:project-occurrence
            (fo-cnf:occurrence ?source ?index))
          (fo-resolution:occurrence
            (fo-resolution:project-source-digest ?source) ?index)))
      (body))

    (rule project-formula-name-atomic
      (head
        (metta-equation
          (fo-resolution:project-formula-name
            (fo-cnf:formula-name-atomic ?name))
          (fo-resolution:formula-name-atomic
            (fo-resolution:project-symbol-name ?name))))
      (body))

    (rule project-formula-name-integer
      (head
        (metta-equation
          (fo-resolution:project-formula-name
            (fo-cnf:formula-name-integer ?lexeme))
          (fo-resolution:formula-name-integer ?lexeme)))
      (body))

    (rule project-role
      (head
        (metta-equation
          (fo-resolution:project-role (fo-cnf:role ?base ?refinement))
          (fo-resolution:role ?base)))
      (body))

    (rule project-symbol-lower
      (head
        (metta-equation
          (fo-resolution:project-symbol-kind (fo-cnf:symbol-lower))
          (fo-resolution:symbol-lower)))
      (body))

    (rule project-symbol-quoted
      (head
        (metta-equation
          (fo-resolution:project-symbol-kind (fo-cnf:symbol-quoted))
          (fo-resolution:symbol-quoted)))
      (body))

    (rule project-symbol-defined
      (head
        (metta-equation
          (fo-resolution:project-symbol-kind (fo-cnf:symbol-defined))
          (fo-resolution:symbol-defined)))
      (body))

    (rule project-symbol-system
      (head
        (metta-equation
          (fo-resolution:project-symbol-kind (fo-cnf:symbol-system))
          (fo-resolution:symbol-system)))
      (body))

    (rule project-symbol-name
      (head
        (metta-equation
          (fo-resolution:project-symbol-name
            (fo-cnf:symbol-name ?kind ?value))
          (fo-resolution:symbol-name
            (fo-resolution:project-symbol-kind ?kind) ?value)))
      (body))

    (rule project-variable-name
      (head
        (metta-equation
          (fo-resolution:project-variable-name
            (fo-cnf:variable-name ?value))
          (fo-resolution:variable-name ?value)))
      (body))

    (rule project-variable-id
      (head
        (metta-equation
          (fo-resolution:project-variable-id
            (fo-cnf:variable-id ?scope ?name))
          (fo-resolution:variable-id
            (fo-resolution:project-occurrence ?scope)
            (fo-resolution:project-variable-name ?name))))
      (body))

    (rule project-number-integer
      (head
        (metta-equation
          (fo-resolution:project-numeric-kind (fo-cnf:number-integer))
          (fo-resolution:number-integer)))
      (body))

    (rule project-number-rational
      (head
        (metta-equation
          (fo-resolution:project-numeric-kind (fo-cnf:number-rational))
          (fo-resolution:number-rational)))
      (body))

    (rule project-number-real
      (head
        (metta-equation
          (fo-resolution:project-numeric-kind (fo-cnf:number-real))
          (fo-resolution:number-real)))
      (body))

    (rule project-term-variable
      (head
        (metta-equation
          (fo-resolution:project-term (fo-cnf:term-variable ?variable))
          (fo-resolution:term-variable
            (fo-resolution:project-variable-id ?variable))))
      (body))

    (rule project-term-function
      (head
        (metta-equation
          (fo-resolution:project-term
            (fo-cnf:term-function ?function ?arguments))
          (fo-resolution:term-function
            (fo-resolution:project-symbol-name ?function)
            (fo-resolution:project-terms ?arguments))))
      (body))

    (rule project-term-number
      (head
        (metta-equation
          (fo-resolution:project-term (fo-cnf:term-number ?kind ?lexeme))
          (fo-resolution:term-number
            (fo-resolution:project-numeric-kind ?kind) ?lexeme)))
      (body))

    (rule project-term-distinct
      (head
        (metta-equation
          (fo-resolution:project-term (fo-cnf:term-distinct ?lexeme))
          (fo-resolution:term-distinct ?lexeme)))
      (body))

    (rule project-terms-nil
      (head
        (metta-equation
          (fo-resolution:project-terms (fo-cnf:terms-nil))
          (fo-resolution:terms-nil)))
      (body))

    (rule project-terms-cons
      (head
        (metta-equation
          (fo-resolution:project-terms
            (fo-cnf:terms-cons ?first ?rest))
          (fo-resolution:terms-cons
            (fo-resolution:project-term ?first)
            (fo-resolution:project-terms ?rest))))
      (body))

    (rule project-atom-predicate
      (head
        (metta-equation
          (fo-resolution:project-atom
            (fo-cnf:atom-predicate ?predicate ?arguments))
          (fo-resolution:atom-predicate
            (fo-resolution:project-symbol-name ?predicate)
            (fo-resolution:project-terms ?arguments))))
      (body))

    (rule project-atom-equality
      (head
        (metta-equation
          (fo-resolution:project-atom (fo-cnf:atom-equality ?left ?right))
          (fo-resolution:atom-equality
            (fo-resolution:project-term ?left)
            (fo-resolution:project-term ?right))))
      (body))

    (rule project-literal-positive
      (head
        (metta-equation
          (fo-resolution:project-literal (fo-cnf:literal-positive ?atom))
          (fo-resolution:literal-positive
            (fo-resolution:project-atom ?atom))))
      (body))

    (rule project-literal-negative
      (head
        (metta-equation
          (fo-resolution:project-literal (fo-cnf:literal-negative ?atom))
          (fo-resolution:literal-negative
            (fo-resolution:project-atom ?atom))))
      (body))

    (rule project-literals-nil
      (head
        (metta-equation
          (fo-resolution:project-literals (fo-cnf:literals-nil))
          (fo-resolution:literals-nil)))
      (body))

    (rule project-literals-cons
      (head
        (metta-equation
          (fo-resolution:project-literals
            (fo-cnf:literals-cons ?first ?rest))
          (fo-resolution:literals-cons
            (fo-resolution:project-literal ?first)
            (fo-resolution:project-literals ?rest))))
      (body))

    (rule project-clause
      (head
        (metta-equation
          (fo-resolution:project-clause
            (fo-cnf:clause ?occurrence ?name ?role ?literals
              ?annotation ?span))
          (fo-resolution:clause
            (fo-resolution:project-occurrence ?occurrence)
            (fo-resolution:project-formula-name ?name)
            (fo-resolution:project-role ?role)
            (fo-resolution:project-literals ?literals))))
      (body))

    (rule project-clauses-nil
      (head
        (metta-equation
          (fo-resolution:project-clauses (fo-cnf:clauses-nil))
          (fo-resolution:clauses-nil)))
      (body))

    (rule project-clauses-cons
      (head
        (metta-equation
          (fo-resolution:project-clauses
            (fo-cnf:clauses-cons ?first ?rest))
          (fo-resolution:clauses-cons
            (fo-resolution:project-clause ?first)
            (fo-resolution:project-clauses ?rest))))
      (body))

    (rule project-problem
      (head
        (metta-equation
          (fo-resolution:project-problem
            (fo-cnf:problem ?source ?clauses))
          (fo-resolution:problem
            (fo-resolution:project-source-digest ?source)
            (fo-resolution:project-clauses ?clauses))))
      (body))))
