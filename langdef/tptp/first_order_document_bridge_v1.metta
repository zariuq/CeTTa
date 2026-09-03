(gslt-presentation-v1 TptpFirstOrderDocumentBridgeV1
  (signature
    (operator metta-equation 2)
    (operator node 2)
    (operator pair 2)
    (operator cons 2)
    (operator cp 1)
    (operator some 1)
    (operator let 3)
    (operator metta-nullary 1)
    (operator cons-atom 2)
    (operator + 2)
    (operator langdef:codepoints->string 1)

    ; Qualified constructors supplied by the syntax-tree/ClauseData bridge.
    (operator tptp:syntax-tree->raw 1)
    (operator tptp:parse->syntax-tree 1)
    (operator tptp:empty 1)
    (operator tptp:formula-name 1)
    (operator tptp:symbol-name 1)
    (operator tptp:role 1)
    (operator tptp:annotation 1)
    (operator tptp:term 2)
    (operator tptp:predicate 2)
    (operator tptp:literal 2)
    (operator LangDef:ImportPrepared 4)
    (operator LangDef:ImportReady 2)

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
    (operator fo-cnf:term-variable 1)
    (operator fo-cnf:term-function 2)
    (operator fo-cnf:term-number 2)
    (operator fo-cnf:term-distinct 1)
    (operator fo-cnf:terms-nil 0)
    (operator fo-cnf:terms-cons 2)
    (operator fo-cnf:number-integer 0)
    (operator fo-cnf:number-rational 0)
    (operator fo-cnf:number-real 0)
    (operator fo-cnf:atom-predicate 2)
    (operator fo-cnf:atom-equality 2)
    (operator fo-cnf:literal-positive 1)
    (operator fo-cnf:literal-negative 1)
    (operator fo-cnf:annotation-none 0)
    (operator fo-cnf:annotation-tree 1)
    (operator fo-cnf:syntax-atom 1)
    (operator fo-cnf:syntax-string 1)
    (operator fo-cnf:syntax-integer 1)
    (operator fo-cnf:syntax-list 1)
    (operator fo-cnf:syntax-nodes-nil 0)
    (operator fo-cnf:syntax-nodes-cons 2)

    (operator tptp-doc:project-formula-name 1)
    (operator tptp-doc:project-role 1)
    (operator tptp-doc:project-symbol 1)
    (operator tptp-doc:project-variable-id 1)
    (operator tptp-doc:project-term 1)
    (operator tptp-doc:project-terms 1)
    (operator tptp-doc:project-atom 1)
    (operator tptp-doc:project-literal 1)
    (operator tptp-doc:project-annotation 1)
    (operator tptp-doc:project-syntax 1)
    (operator tptp-doc:project-syntax-nodes 1)
    (operator tptp-doc:formula 2)
    (operator tptp-doc:and-chain 3)
    (operator tptp-doc:or-chain 3)
    (operator tptp-doc:clause-formula 2)
    (operator tptp-doc:clause-rest 3)
    (operator tptp-doc:variables 2)
    (operator tptp-doc:variable-rest 2)
    (operator tptp-doc:formula-names 1)
    (operator tptp-doc:formula-name-rest 1)
    (operator tptp-doc:include-selection 1)
    (operator tptp-doc:optional-formula-name 1)
    (operator tptp-doc:input 3)
    (operator tptp-doc:inputs 3)
    (operator tptp-doc:ast->document 2)
    (operator tptp-doc:syntax-tree->document 2)
    (operator tptp-doc:parse->document 1)
    (operator tptp-doc:parse->import 1)

    (operator tptp-fo:source-digest 1)
    (operator tptp-fo:occurrence 2)
    (operator tptp-fo:formula-name-atomic 1)
    (operator tptp-fo:formula-name-integer 1)
    (operator tptp-fo:formula-names-nil 0)
    (operator tptp-fo:formula-names-cons 2)
    (operator tptp-fo:formula-name-none 0)
    (operator tptp-fo:formula-name-some 1)
    (operator tptp-fo:role 2)
    (operator tptp-fo:dialect-fof 0)
    (operator tptp-fo:dialect-cnf 0)
    (operator tptp-fo:symbol-lower 0)
    (operator tptp-fo:symbol-quoted 0)
    (operator tptp-fo:symbol-defined 0)
    (operator tptp-fo:symbol-system 0)
    (operator tptp-fo:symbol-name 2)
    (operator tptp-fo:variable-name 1)
    (operator tptp-fo:variable-id 2)
    (operator tptp-fo:variables-nil 0)
    (operator tptp-fo:variables-cons 2)
    (operator tptp-fo:number-integer 0)
    (operator tptp-fo:number-rational 0)
    (operator tptp-fo:number-real 0)
    (operator tptp-fo:term-variable 1)
    (operator tptp-fo:term-function 2)
    (operator tptp-fo:term-number 2)
    (operator tptp-fo:term-distinct 1)
    (operator tptp-fo:terms-nil 0)
    (operator tptp-fo:terms-cons 2)
    (operator tptp-fo:atom-predicate 2)
    (operator tptp-fo:atom-equality 2)
    (operator tptp-fo:formula-atom 1)
    (operator tptp-fo:formula-not 1)
    (operator tptp-fo:formula-and 2)
    (operator tptp-fo:formula-or 2)
    (operator tptp-fo:formula-iff 2)
    (operator tptp-fo:formula-implies 2)
    (operator tptp-fo:formula-reverse-implies 2)
    (operator tptp-fo:formula-xor 2)
    (operator tptp-fo:formula-nor 2)
    (operator tptp-fo:formula-nand 2)
    (operator tptp-fo:formula-forall 2)
    (operator tptp-fo:formula-exists 2)
    (operator tptp-fo:syntax-atom 1)
    (operator tptp-fo:syntax-string 1)
    (operator tptp-fo:syntax-integer 1)
    (operator tptp-fo:syntax-list 1)
    (operator tptp-fo:syntax-nodes-nil 0)
    (operator tptp-fo:syntax-nodes-cons 2)
    (operator tptp-fo:annotation-none 0)
    (operator tptp-fo:annotation-tree 1)
    (operator tptp-fo:source-span-unknown 0)
    (operator tptp-fo:include-all 0)
    (operator tptp-fo:include-names 1)
    (operator tptp-fo:formula-input 7)
    (operator tptp-fo:include-input 5)
    (operator tptp-fo:inputs-nil 0)
    (operator tptp-fo:inputs-cons 2)
    (operator tptp-fo:document 2))
  (equations)
  (rewrites
    (rule project-formula-name-atomic
      (head (metta-equation
        (tptp-doc:project-formula-name
          (fo-cnf:formula-name-atomic ?symbol))
        (tptp-fo:formula-name-atomic
          (tptp-doc:project-symbol ?symbol))))
      (body))
    (rule project-formula-name-integer
      (head (metta-equation
        (tptp-doc:project-formula-name
          (fo-cnf:formula-name-integer ?lexeme))
        (tptp-fo:formula-name-integer ?lexeme)))
      (body))
    (rule project-role
      (head (metta-equation
        (tptp-doc:project-role (fo-cnf:role ?base ?refinement))
        (tptp-fo:role ?base
          (tptp-doc:project-annotation ?refinement))))
      (body))

    (rule project-symbol-lower
      (head (metta-equation
        (tptp-doc:project-symbol
          (fo-cnf:symbol-name (fo-cnf:symbol-lower) ?value))
        (tptp-fo:symbol-name (tptp-fo:symbol-lower) ?value)))
      (body))
    (rule project-symbol-quoted
      (head (metta-equation
        (tptp-doc:project-symbol
          (fo-cnf:symbol-name (fo-cnf:symbol-quoted) ?value))
        (tptp-fo:symbol-name (tptp-fo:symbol-quoted) ?value)))
      (body))
    (rule project-symbol-defined
      (head (metta-equation
        (tptp-doc:project-symbol
          (fo-cnf:symbol-name (fo-cnf:symbol-defined) ?value))
        (tptp-fo:symbol-name (tptp-fo:symbol-defined) ?value)))
      (body))
    (rule project-symbol-system
      (head (metta-equation
        (tptp-doc:project-symbol
          (fo-cnf:symbol-name (fo-cnf:symbol-system) ?value))
        (tptp-fo:symbol-name (tptp-fo:symbol-system) ?value)))
      (body))

    (rule project-variable-id
      (head (metta-equation
        (tptp-doc:project-variable-id
          (fo-cnf:variable-id ?scope (fo-cnf:variable-name ?name)))
        (tptp-fo:variable-id ?scope (tptp-fo:variable-name ?name))))
      (body))
    (rule project-term-variable
      (head (metta-equation
        (tptp-doc:project-term (fo-cnf:term-variable ?variable))
        (tptp-fo:term-variable
          (tptp-doc:project-variable-id ?variable))))
      (body))
    (rule project-term-function
      (head (metta-equation
        (tptp-doc:project-term (fo-cnf:term-function ?symbol ?arguments))
        (tptp-fo:term-function
          (tptp-doc:project-symbol ?symbol)
          (tptp-doc:project-terms ?arguments))))
      (body))
    (rule project-term-number-integer
      (head (metta-equation
        (tptp-doc:project-term
          (fo-cnf:term-number (fo-cnf:number-integer) ?lexeme))
        (tptp-fo:term-number (tptp-fo:number-integer) ?lexeme)))
      (body))
    (rule project-term-number-rational
      (head (metta-equation
        (tptp-doc:project-term
          (fo-cnf:term-number (fo-cnf:number-rational) ?lexeme))
        (tptp-fo:term-number (tptp-fo:number-rational) ?lexeme)))
      (body))
    (rule project-term-number-real
      (head (metta-equation
        (tptp-doc:project-term
          (fo-cnf:term-number (fo-cnf:number-real) ?lexeme))
        (tptp-fo:term-number (tptp-fo:number-real) ?lexeme)))
      (body))
    (rule project-term-distinct
      (head (metta-equation
        (tptp-doc:project-term (fo-cnf:term-distinct ?lexeme))
        (tptp-fo:term-distinct ?lexeme)))
      (body))
    (rule project-terms-nil
      (head (metta-equation
        (tptp-doc:project-terms (fo-cnf:terms-nil))
        (tptp-fo:terms-nil)))
      (body))
    (rule project-terms-cons
      (head (metta-equation
        (tptp-doc:project-terms (fo-cnf:terms-cons ?first ?rest))
        (tptp-fo:terms-cons
          (tptp-doc:project-term ?first)
          (tptp-doc:project-terms ?rest))))
      (body))
    (rule project-atom-predicate
      (head (metta-equation
        (tptp-doc:project-atom (fo-cnf:atom-predicate ?symbol ?arguments))
        (tptp-fo:atom-predicate
          (tptp-doc:project-symbol ?symbol)
          (tptp-doc:project-terms ?arguments))))
      (body))
    (rule project-atom-equality
      (head (metta-equation
        (tptp-doc:project-atom (fo-cnf:atom-equality ?left ?right))
        (tptp-fo:atom-equality
          (tptp-doc:project-term ?left)
          (tptp-doc:project-term ?right))))
      (body))
    (rule project-literal-positive
      (head (metta-equation
        (tptp-doc:project-literal (fo-cnf:literal-positive ?atom))
        (tptp-fo:formula-atom (tptp-doc:project-atom ?atom))))
      (body))
    (rule project-literal-negative
      (head (metta-equation
        (tptp-doc:project-literal (fo-cnf:literal-negative ?atom))
        (tptp-fo:formula-not
          (tptp-fo:formula-atom (tptp-doc:project-atom ?atom)))))
      (body))

    (rule project-annotation-none
      (head (metta-equation
        (tptp-doc:project-annotation (fo-cnf:annotation-none))
        (tptp-fo:annotation-none)))
      (body))
    (rule project-annotation-tree
      (head (metta-equation
        (tptp-doc:project-annotation (fo-cnf:annotation-tree ?tree))
        (tptp-fo:annotation-tree (tptp-doc:project-syntax ?tree))))
      (body))
    (rule project-syntax-atom
      (head (metta-equation
        (tptp-doc:project-syntax (fo-cnf:syntax-atom ?value))
        (tptp-fo:syntax-atom ?value)))
      (body))
    (rule project-syntax-string
      (head (metta-equation
        (tptp-doc:project-syntax (fo-cnf:syntax-string ?value))
        (tptp-fo:syntax-string ?value)))
      (body))
    (rule project-syntax-integer
      (head (metta-equation
        (tptp-doc:project-syntax (fo-cnf:syntax-integer ?value))
        (tptp-fo:syntax-integer ?value)))
      (body))
    (rule project-syntax-list
      (head (metta-equation
        (tptp-doc:project-syntax (fo-cnf:syntax-list ?items))
        (tptp-fo:syntax-list (tptp-doc:project-syntax-nodes ?items))))
      (body))
    (rule project-syntax-nodes-nil
      (head (metta-equation
        (tptp-doc:project-syntax-nodes (fo-cnf:syntax-nodes-nil))
        (tptp-fo:syntax-nodes-nil)))
      (body))
    (rule project-syntax-nodes-cons
      (head (metta-equation
        (tptp-doc:project-syntax-nodes
          (fo-cnf:syntax-nodes-cons ?first ?rest))
        (tptp-fo:syntax-nodes-cons
          (tptp-doc:project-syntax ?first)
          (tptp-doc:project-syntax-nodes ?rest))))
      (body))

    (rule formula-atomic
      (head (metta-equation
        (tptp-doc:formula (node atomic ?predicate) ?scope)
        (tptp-fo:formula-atom
          (tptp-doc:project-atom (tptp:predicate ?predicate ?scope)))))
      (body))
    (rule formula-equality
      (head (metta-equation
        (tptp-doc:formula (node equality (pair ?left ?right)) ?scope)
        (tptp-fo:formula-atom
          (tptp-fo:atom-equality
            (tptp-doc:project-term (tptp:term ?left ?scope))
            (tptp-doc:project-term (tptp:term ?right ?scope))))))
      (body))
    (rule formula-disequality
      (head (metta-equation
        (tptp-doc:formula (node disequality (pair ?left ?right)) ?scope)
        (tptp-fo:formula-not
          (tptp-fo:formula-atom
            (tptp-fo:atom-equality
              (tptp-doc:project-term (tptp:term ?left ?scope))
              (tptp-doc:project-term (tptp:term ?right ?scope)))))))
      (body))
    (rule formula-negation
      (head (metta-equation
        (tptp-doc:formula (node negation ?body) ?scope)
        (tptp-fo:formula-not (tptp-doc:formula ?body ?scope))))
      (body))
    (rule formula-paren
      (head (metta-equation
        (tptp-doc:formula (node paren ?body) ?scope)
        (tptp-doc:formula ?body ?scope)))
      (body))
    (rule formula-and
      (head (metta-equation
        (tptp-doc:formula (node and (pair ?first ?rest)) ?scope)
        (tptp-doc:and-chain ?first ?rest ?scope)))
      (body))
    (rule and-chain-last
      (head (metta-equation
        (tptp-doc:and-chain ?first (cons ?second nil) ?scope)
        (tptp-fo:formula-and
          (tptp-doc:formula ?first ?scope)
          (tptp-doc:formula ?second ?scope))))
      (body))
    (rule and-chain-cons
      (head (metta-equation
        (tptp-doc:and-chain
          ?first (cons ?second (cons ?third ?rest)) ?scope)
        (tptp-fo:formula-and
          (tptp-doc:formula ?first ?scope)
          (tptp-doc:and-chain
            ?second (cons ?third ?rest) ?scope))))
      (body))
    (rule formula-or
      (head (metta-equation
        (tptp-doc:formula (node or (pair ?first ?rest)) ?scope)
        (tptp-doc:or-chain ?first ?rest ?scope)))
      (body))
    (rule or-chain-last
      (head (metta-equation
        (tptp-doc:or-chain ?first (cons ?second nil) ?scope)
        (tptp-fo:formula-or
          (tptp-doc:formula ?first ?scope)
          (tptp-doc:formula ?second ?scope))))
      (body))
    (rule or-chain-cons
      (head (metta-equation
        (tptp-doc:or-chain
          ?first (cons ?second (cons ?third ?rest)) ?scope)
        (tptp-fo:formula-or
          (tptp-doc:formula ?first ?scope)
          (tptp-doc:or-chain
            ?second (cons ?third ?rest) ?scope))))
      (body))
    (rule formula-iff
      (head (metta-equation
        (tptp-doc:formula (node iff (pair ?left ?right)) ?scope)
        (tptp-fo:formula-iff
          (tptp-doc:formula ?left ?scope)
          (tptp-doc:formula ?right ?scope))))
      (body))
    (rule formula-implies
      (head (metta-equation
        (tptp-doc:formula (node implies (pair ?left ?right)) ?scope)
        (tptp-fo:formula-implies
          (tptp-doc:formula ?left ?scope)
          (tptp-doc:formula ?right ?scope))))
      (body))
    (rule formula-reverse-implies
      (head (metta-equation
        (tptp-doc:formula (node reverse-implies (pair ?left ?right)) ?scope)
        (tptp-fo:formula-reverse-implies
          (tptp-doc:formula ?left ?scope)
          (tptp-doc:formula ?right ?scope))))
      (body))
    (rule formula-xor
      (head (metta-equation
        (tptp-doc:formula (node xor (pair ?left ?right)) ?scope)
        (tptp-fo:formula-xor
          (tptp-doc:formula ?left ?scope)
          (tptp-doc:formula ?right ?scope))))
      (body))
    (rule formula-nor
      (head (metta-equation
        (tptp-doc:formula (node nor (pair ?left ?right)) ?scope)
        (tptp-fo:formula-nor
          (tptp-doc:formula ?left ?scope)
          (tptp-doc:formula ?right ?scope))))
      (body))
    (rule formula-nand
      (head (metta-equation
        (tptp-doc:formula (node nand (pair ?left ?right)) ?scope)
        (tptp-fo:formula-nand
          (tptp-doc:formula ?left ?scope)
          (tptp-doc:formula ?right ?scope))))
      (body))
    (rule formula-forall
      (head (metta-equation
        (tptp-doc:formula (node forall (pair ?variables ?body)) ?scope)
        (tptp-fo:formula-forall
          (tptp-doc:variables ?variables ?scope)
          (tptp-doc:formula ?body ?scope))))
      (body))
    (rule formula-exists
      (head (metta-equation
        (tptp-doc:formula (node exists (pair ?variables ?body)) ?scope)
        (tptp-fo:formula-exists
          (tptp-doc:variables ?variables ?scope)
          (tptp-doc:formula ?body ?scope))))
      (body))
    (rule variables
      (head (metta-equation
        (tptp-doc:variables
          (node variable-list (pair (node upper-word ?name) ?rest)) ?scope)
        (tptp-fo:variables-cons
          (tptp-fo:variable-id ?scope
            (tptp-fo:variable-name
              (langdef:codepoints->string ?name)))
          (tptp-doc:variable-rest ?rest ?scope))))
      (body))
    (rule variable-rest-cons
      (head (metta-equation
        (tptp-doc:variable-rest
          (cons (node upper-word ?name) ?rest) ?scope)
        (tptp-fo:variables-cons
          (tptp-fo:variable-id ?scope
            (tptp-fo:variable-name
              (langdef:codepoints->string ?name)))
          (tptp-doc:variable-rest ?rest ?scope))))
      (body))
    (rule variable-rest-nil
      (head (metta-equation
        (tptp-doc:variable-rest nil ?scope)
        (tptp-fo:variables-nil)))
      (body))

    (rule clause-formula-single
      (head (metta-equation
        (tptp-doc:clause-formula
          (node cnf-disjunction (pair ?first nil)) ?scope)
        (tptp-doc:project-literal (tptp:literal ?first ?scope))))
      (body))
    (rule clause-formula-many
      (head (metta-equation
        (tptp-doc:clause-formula
          (node cnf-disjunction (pair ?first (cons ?second ?rest))) ?scope)
        (tptp-fo:formula-or
          (tptp-doc:project-literal (tptp:literal ?first ?scope))
          (tptp-doc:clause-rest ?second ?rest ?scope))))
      (body))
    (rule clause-rest-last
      (head (metta-equation
        (tptp-doc:clause-rest ?first nil ?scope)
        (tptp-doc:project-literal (tptp:literal ?first ?scope))))
      (body))
    (rule clause-rest-cons
      (head (metta-equation
        (tptp-doc:clause-rest ?first (cons ?second ?rest) ?scope)
        (tptp-fo:formula-or
          (tptp-doc:project-literal (tptp:literal ?first ?scope))
          (tptp-doc:clause-rest ?second ?rest ?scope))))
      (body))

    (rule formula-names
      (head (metta-equation
        (tptp-doc:formula-names (node name-list (pair ?first ?rest)))
        (tptp-fo:formula-names-cons
          (tptp-doc:project-formula-name (tptp:formula-name ?first))
          (tptp-doc:formula-name-rest ?rest))))
      (body))
    (rule formula-name-rest-cons
      (head (metta-equation
        (tptp-doc:formula-name-rest (cons ?first ?rest))
        (tptp-fo:formula-names-cons
          (tptp-doc:project-formula-name (tptp:formula-name ?first))
          (tptp-doc:formula-name-rest ?rest))))
      (body))
    (rule formula-name-rest-nil
      (head (metta-equation
        (tptp-doc:formula-name-rest nil)
        (tptp-fo:formula-names-nil)))
      (body))
    (rule include-selection-all
      (head (metta-equation
        (tptp-doc:include-selection (cons (cp 42) nil))
        (tptp-fo:include-all)))
      (body))
    (rule include-selection-names
      (head (metta-equation
        (tptp-doc:include-selection (node name-list ?names))
        (tptp-fo:include-names
          (tptp-doc:formula-names (node name-list ?names)))))
      (body))
    (rule optional-formula-name-none
      (head (metta-equation
        (tptp-doc:optional-formula-name none)
        (tptp-fo:formula-name-none)))
      (body))
    (rule optional-formula-name-some
      (head (metta-equation
        (tptp-doc:optional-formula-name (some ?name))
        (tptp-fo:formula-name-some
          (tptp-doc:project-formula-name (tptp:formula-name ?name)))))
      (body))

    (rule input-fof
      (head (metta-equation
        (tptp-doc:input
          (node fof (pair ?name (pair ?role (pair ?formula ?annotation))))
          ?source ?index)
        (let ?scope (tptp-fo:occurrence ?source ?index)
          (tptp-fo:formula-input
            ?scope
            (tptp-fo:dialect-fof)
            (tptp-doc:project-formula-name (tptp:formula-name ?name))
            (tptp-doc:project-role (tptp:role ?role))
            (tptp-doc:formula ?formula ?scope)
            (tptp-doc:project-annotation (tptp:annotation ?annotation))
            (tptp-fo:source-span-unknown)))))
      (body))
    (rule input-cnf
      (head (metta-equation
        (tptp-doc:input
          (node cnf (pair ?name (pair ?role (pair ?formula ?annotation))))
          ?source ?index)
        (let ?scope (tptp-fo:occurrence ?source ?index)
          (tptp-fo:formula-input
            ?scope
            (tptp-fo:dialect-cnf)
            (tptp-doc:project-formula-name (tptp:formula-name ?name))
            (tptp-doc:project-role (tptp:role ?role))
            (tptp-doc:clause-formula ?formula ?scope)
            (tptp-doc:project-annotation (tptp:annotation ?annotation))
            (tptp-fo:source-span-unknown)))))
      (body))
    (rule input-include-bare
      (head (metta-equation
        (tptp-doc:input (node include (pair ?path none)) ?source ?index)
        (tptp-fo:include-input
          (tptp-fo:occurrence ?source ?index)
          (tptp-doc:project-symbol (tptp:symbol-name ?path))
          (tptp-fo:include-all)
          (tptp-fo:formula-name-none)
          (tptp-fo:source-span-unknown))))
      (body))
    (rule input-include-selected
      (head (metta-equation
        (tptp-doc:input
          (node include (pair ?path (some (pair ?selection ?qualifier))))
          ?source ?index)
        (tptp-fo:include-input
          (tptp-fo:occurrence ?source ?index)
          (tptp-doc:project-symbol (tptp:symbol-name ?path))
          (tptp-doc:include-selection ?selection)
          (tptp-doc:optional-formula-name ?qualifier)
          (tptp-fo:source-span-unknown))))
      (body))
    (rule inputs-cons
      (head (metta-equation
        (tptp-doc:inputs (cons ?first ?rest) ?source ?index)
        (tptp-fo:inputs-cons
          (tptp-doc:input ?first ?source ?index)
          (tptp-doc:inputs ?rest ?source (+ ?index 1)))))
      (body))
    (rule inputs-nil
      (head (metta-equation
        (tptp-doc:inputs nil ?source ?index)
        (tptp-fo:inputs-nil)))
      (body))
    (rule ast-to-document
      (head (metta-equation
        (tptp-doc:ast->document (node tptp-file ?inputs) ?digest)
        (let ?source (tptp-fo:source-digest ?digest)
          (tptp-fo:document ?source
            (tptp-doc:inputs ?inputs ?source 0)))))
      (body))
    (rule syntax-tree-to-document
      (head (metta-equation
        (tptp-doc:syntax-tree->document ?syntax-tree ?digest)
        (tptp-doc:ast->document
          (tptp:syntax-tree->raw ?syntax-tree) ?digest)))
      (body))
    (rule parse-to-document
      (head (metta-equation
        (tptp-doc:parse->document
          (LangDef:ImportPrepared TptpFofCnfV1 ?pack ?digest ?values))
        (tptp-doc:syntax-tree->document
          (tptp:parse->syntax-tree
            (LangDef:ImportPrepared TptpFofCnfV1 ?pack ?digest ?values))
          ?digest)))
      (body))
    (rule parse-to-import
      (head (metta-equation
        (tptp-doc:parse->import
          (LangDef:ImportPrepared TptpFofCnfV1 ?pack ?digest ?values))
        (let ?syntax-tree
            (tptp:parse->syntax-tree
              (LangDef:ImportPrepared TptpFofCnfV1 ?pack ?digest ?values))
          (let ?document
              (tptp-doc:syntax-tree->document ?syntax-tree ?digest)
            (let ?empty (tptp:empty empty)
              (LangDef:ImportReady ?syntax-tree
                (cons-atom ?document ?empty)))))))
      (body))))
