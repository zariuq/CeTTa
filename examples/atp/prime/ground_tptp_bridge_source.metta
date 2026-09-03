; Historical ground-CNF bridge retained as an explicit Prime ATP example.
; It is not part of lib_tptp and does not define TPTP semantics.
(gslt-presentation-v1 GroundTptpAtpBridgeExampleV1
  (signature
    (operator metta-equation 2)
    (operator node 2)
    (operator pair 2)
    (operator cons 2)
    (operator let 3)
    (operator if 3)
    (operator == 2)
    (operator collapse 1)
    (operator metta-nullary 1)
    (operator cons-atom 2)
    (operator langdef:codepoints->symbol 1)
    (operator tptp:word 1)
    (operator tptp:name 1)
    (operator tptp:role 1)
    (operator tptp:term 1)
    (operator tptp:arguments 1)
    (operator tptp:term-rest 1)
    (operator tptp:atomic 1)
    (operator tptp:literal 1)
    (operator tptp:clause 1)
    (operator tptp:literal-rest 1)
    (operator tptp:cnf 1)
    (operator tptp:inputs 1)
    (operator tptp:prepend 2)
    (operator tptp:parse->atp 1)
    (operator tptp-atp:parse->import 1)
    (operator tptp:atp->import-protocol 1)
    (operator tptp:ast->atp 1)
    (operator tptp:ground-both 2)
    (operator tptp:ground-file 1)
    (operator tptp:ground-inputs 1)
    (operator tptp:ground-cnf 1)
    (operator tptp:ground-name 1)
    (operator tptp:ground-role 1)
    (operator tptp:ground-role-value 1)
    (operator tptp:ground-clause 1)
    (operator tptp:ground-literal 1)
    (operator tptp:ground-literal-rest 1)
    (operator tptp:ground-atomic 1)
    (operator tptp:ground-term 1)
    (operator tptp:ground-arguments 1)
    (operator tptp:ground-term-rest 1)
    (operator tptp:empty 1)
    (operator ATP.Pos 1)
    (operator ATP.Neg 1)
    (operator ATP.NamedClause 2)
    (operator TPTP.Clause 2)
    (operator TPTP.ATPInput 2)
    (operator TPTP.ATPReady 1)
    (operator TPTP.Unsupported 1)
    (operator LangDef:ParseAccepted 3)
    (operator LangDef:ImportReady 1)
    (operator LangDef:ImportRejected 1))
  (equations)
  (rewrites
    (rule eq-arguments
      (head
        (metta-equation
          (tptp:arguments (pair ?first ?rest))
          (let ?head (tptp:term ?first)
            (let ?tail (tptp:term-rest ?rest)
              (cons-atom ?head ?tail)))))
      (body))

    (rule eq-ast-to-atp
      (head
        (metta-equation
          (tptp:ast->atp (node tptp-file ?inputs))
          (tptp:inputs ?inputs)))
      (body))

    (rule eq-atp-ready-to-import
      (head
        (metta-equation
          (tptp:atp->import-protocol (TPTP.ATPReady ?input))
          (let ?empty (tptp:empty empty)
            (LangDef:ImportReady (cons-atom ?input ?empty)))))
      (body))

    (rule eq-atp-unsupported-to-import
      (head
        (metta-equation
          (tptp:atp->import-protocol (TPTP.Unsupported ?reason))
          (LangDef:ImportRejected (TPTP.Unsupported ?reason))))
      (body))

    (rule eq-atomic
      (head
        (metta-equation
          (tptp:atomic (node atomic ?term))
          (tptp:term ?term)))
      (body))

    (rule eq-clause
      (head
        (metta-equation
          (tptp:clause (node cnf-disjunction (pair ?first ?rest)))
          (let ?head (tptp:literal ?first)
            (let ?tail (tptp:literal-rest ?rest)
              (cons-atom ?head ?tail)))))
      (body))

    (rule eq-cnf
      (head
        (metta-equation
          (tptp:cnf
            (node cnf
              (pair ?name
                (pair ?role
                  (pair ?formula ?annotations)))))
          (let ?clause-name (tptp:name ?name)
            (let ?clause-role (tptp:role ?role)
              (let ?literals (tptp:clause ?formula)
                (TPTP.Clause ?clause-role
                  (ATP.NamedClause ?clause-name ?literals)))))))
      (body))

    (rule eq-empty
      (head
        (metta-equation
          (tptp:empty empty)
          (collapse (metta-nullary empty))))
      (body))

    (rule eq-ground-arguments
      (head
        (metta-equation
          (tptp:ground-arguments (pair ?first ?rest))
          (let ?head (tptp:ground-term ?first)
            (let ?tail (tptp:ground-term-rest ?rest)
              (tptp:ground-both ?head ?tail)))))
      (body))

    (rule eq-ground-atomic
      (head
        (metta-equation
          (tptp:ground-atomic (node atomic ?term))
          (tptp:ground-term ?term)))
      (body))

    (rule eq-ground-both
      (head
        (metta-equation
          (tptp:ground-both True True)
          True))
      (body))

    (rule eq-ground-clause
      (head
        (metta-equation
          (tptp:ground-clause
            (node cnf-disjunction (pair ?first ?rest)))
          (let ?head (tptp:ground-literal ?first)
            (let ?tail (tptp:ground-literal-rest ?rest)
              (tptp:ground-both ?head ?tail)))))
      (body))

    (rule eq-ground-cnf
      (head
        (metta-equation
          (tptp:ground-cnf
            (node cnf
              (pair ?name
                (pair ?role
                  (pair ?formula none)))))
          (let ?name-ok (tptp:ground-name ?name)
            (let ?role-ok (tptp:ground-role ?role)
              (let ?formula-ok (tptp:ground-clause ?formula)
                (tptp:ground-both ?name-ok
                  (tptp:ground-both ?role-ok ?formula-ok)))))))
      (body))

    (rule eq-ground-file
      (head
        (metta-equation
          (tptp:ground-file (node tptp-file ?inputs))
          (tptp:ground-inputs ?inputs)))
      (body))

    (rule eq-ground-inputs-cons
      (head
        (metta-equation
          (tptp:ground-inputs (cons ?input ?rest))
          (let ?head (tptp:ground-cnf ?input)
            (let ?tail (tptp:ground-inputs ?rest)
              (tptp:ground-both ?head ?tail)))))
      (body))

    (rule eq-ground-inputs-nil
      (head
        (metta-equation
          (tptp:ground-inputs nil)
          True))
      (body))

    (rule eq-ground-literal-negative
      (head
        (metta-equation
          (tptp:ground-literal (node negative-literal ?atomic))
          (tptp:ground-atomic ?atomic)))
      (body))

    (rule eq-ground-literal-positive
      (head
        (metta-equation
          (tptp:ground-literal (node positive-literal ?atomic))
          (tptp:ground-atomic ?atomic)))
      (body))

    (rule eq-ground-literal-rest-cons
      (head
        (metta-equation
          (tptp:ground-literal-rest (cons ?first ?rest))
          (let ?head (tptp:ground-literal ?first)
            (let ?tail (tptp:ground-literal-rest ?rest)
              (tptp:ground-both ?head ?tail)))))
      (body))

    (rule eq-ground-literal-rest-nil
      (head
        (metta-equation
          (tptp:ground-literal-rest nil)
          True))
      (body))

    (rule eq-ground-name-lower
      (head
        (metta-equation
          (tptp:ground-name (node lower-word ?characters))
          True))
      (body))

    (rule eq-ground-role
      (head
        (metta-equation
          (tptp:ground-role ?role)
          (let ?value (tptp:role ?role)
            (tptp:ground-role-value ?value))))
      (body))

    (rule eq-ground-role-axiom
      (head
        (metta-equation
          (tptp:ground-role-value axiom)
          True))
      (body))

    (rule eq-ground-role-negated-conjecture
      (head
        (metta-equation
          (tptp:ground-role-value negated_conjecture)
          True))
      (body))

    (rule eq-ground-term-application
      (head
        (metta-equation
          (tptp:ground-term
            (node application
              (pair (node lower-word ?characters)
                (node arguments ?arguments))))
          (tptp:ground-arguments ?arguments)))
      (body))

    (rule eq-ground-term-constant
      (head
        (metta-equation
          (tptp:ground-term
            (node constant (node lower-word ?characters)))
          True))
      (body))

    (rule eq-ground-term-rest-cons
      (head
        (metta-equation
          (tptp:ground-term-rest (cons ?first ?rest))
          (let ?head (tptp:ground-term ?first)
            (let ?tail (tptp:ground-term-rest ?rest)
              (tptp:ground-both ?head ?tail)))))
      (body))

    (rule eq-ground-term-rest-nil
      (head
        (metta-equation
          (tptp:ground-term-rest nil)
          True))
      (body))

    (rule eq-inputs-empty
      (head
        (metta-equation
          (tptp:inputs nil)
          (let ?usable (tptp:empty empty)
            (let ?support (tptp:empty empty)
              (TPTP.ATPInput ?usable ?support)))))
      (body))

    (rule eq-inputs-cons
      (head
        (metta-equation
          (tptp:inputs (cons ?input ?rest))
          (let ?clause (tptp:cnf ?input)
            (let ?tail (tptp:inputs ?rest)
              (tptp:prepend ?clause ?tail)))))
      (body))

    (rule eq-literal-negative
      (head
        (metta-equation
          (tptp:literal (node negative-literal ?atomic))
          (let ?value (tptp:atomic ?atomic)
            (ATP.Neg ?value))))
      (body))

    (rule eq-literal-positive
      (head
        (metta-equation
          (tptp:literal (node positive-literal ?atomic))
          (let ?value (tptp:atomic ?atomic)
            (ATP.Pos ?value))))
      (body))

    (rule eq-literal-rest-cons
      (head
        (metta-equation
          (tptp:literal-rest (cons ?first ?rest))
          (let ?head (tptp:literal ?first)
            (let ?tail (tptp:literal-rest ?rest)
              (cons-atom ?head ?tail)))))
      (body))

    (rule eq-literal-rest-nil
      (head
        (metta-equation
          (tptp:literal-rest nil)
          (tptp:empty empty)))
      (body))

    (rule eq-name-lower
      (head
        (metta-equation
          (tptp:name (node lower-word ?characters))
          (tptp:word (node lower-word ?characters))))
      (body))

    (rule eq-prepend-axiom
      (head
        (metta-equation
          (tptp:prepend
            (TPTP.Clause axiom ?clause)
            (TPTP.ATPInput ?usable ?support))
          (TPTP.ATPInput (cons-atom ?clause ?usable) ?support)))
      (body))

    (rule eq-prepend-negated-conjecture
      (head
        (metta-equation
          (tptp:prepend
            (TPTP.Clause negated_conjecture ?clause)
            (TPTP.ATPInput ?usable ?support))
          (TPTP.ATPInput ?usable (cons-atom ?clause ?support))))
      (body))

    (rule eq-parse-to-atp
      (head
        (metta-equation
          (tptp:parse->atp
            (LangDef:ParseAccepted TptpFofCnfV1 ?digest
              (metta-nullary ?ast)))
          (if (== (tptp:ground-file ?ast) True)
            (TPTP.ATPReady (tptp:ast->atp ?ast))
            (TPTP.Unsupported ground-cnf-subset))))
      (body))

    (rule eq-parse-to-import
      (head
        (metta-equation
          (tptp-atp:parse->import ?parsed)
          (tptp:atp->import-protocol (tptp:parse->atp ?parsed))))
      (body))

    (rule eq-role
      (head
        (metta-equation
          (tptp:role
            (node formula-role
              (pair (node lower-word ?characters) ?optional)))
          (tptp:word (node lower-word ?characters))))
      (body))

    (rule eq-term-application
      (head
        (metta-equation
          (tptp:term
            (node application
              (pair ?word (node arguments ?arguments))))
          (let ?function (tptp:word ?word)
            (let ?arguments-value (tptp:arguments ?arguments)
              (cons-atom ?function ?arguments-value)))))
      (body))

    (rule eq-term-constant
      (head
        (metta-equation
          (tptp:term (node constant ?word))
          (tptp:word ?word)))
      (body))

    (rule eq-term-rest-cons
      (head
        (metta-equation
          (tptp:term-rest (cons ?first ?rest))
          (let ?head (tptp:term ?first)
            (let ?tail (tptp:term-rest ?rest)
              (cons-atom ?head ?tail)))))
      (body))

    (rule eq-term-rest-nil
      (head
        (metta-equation
          (tptp:term-rest nil)
          (tptp:empty empty)))
      (body))

    (rule eq-word-lower
      (head
        (metta-equation
          (tptp:word (node lower-word ?characters))
          (langdef:codepoints->symbol ?characters)))
      (body))))
