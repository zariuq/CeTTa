; Canonical TPTP/TSTP text from compact public records.
; The output deliberately normalizes layout and redundant parentheses.
(gslt-presentation-v1 TptpOfficialRecordsToTextV1
  (signature
    (operator metta-equation 2)
    (operator bnf-v1:string->text 1) (operator bnf-v1:text->string 1)
    (operator bnf-v1:text-append 2) (operator bnf-v1:text-nil 0)
    (operator tptp-print-v1:document 1) (operator tptp-print-v1:file 1)
    (operator tptp-print-v1:inputs 1) (operator tptp-print-v1:input 1)
    (operator tptp-print-v1:annotated 5)
    (operator tptp-print-v1:annotated-cnf 4)
    (operator tptp-print-v1:annotated-tcf 4)
    (operator tptp-print-v1:cnf-clause 1)
    (operator tptp-print-v1:tcf-formula 1)
    (operator tptp-print-v1:name 1) (operator tptp-print-v1:word 1)
    (operator tptp-print-v1:role 1) (operator tptp-print-v1:expr 1)
    (operator tptp-print-v1:type-expr 1)
    (operator tptp-print-v1:let-types 1)
    (operator tptp-print-v1:let-definitions 1)
    (operator tptp-print-v1:let-definition-items 1)
    (operator tptp-print-v1:terms 1) (operator tptp-print-v1:vars 1)
    (operator tptp-print-v1:names 1) (operator tptp-print-v1:annotation 1)
    (operator tptp-print-v1:optional 1) (operator tptp-print-v1:selection 1)
    (operator tptp-print-v1:qualification 1)
    (operator tptp-print-v1:include-tail 2)
    (operator tptp-print-v1:cat2 2) (operator tptp-print-v1:cat3 3)
    (operator tptp-print-v1:cat4 4) (operator tptp-print-v1:cat5 5)
    (operator tptp-print-v1:binary 3) (operator tptp-print-v1:quant 3)
    (operator tptp-print-v1:call 2) (operator tptp-print-v1:brackets 1)
    (operator tptp-rec:file 2) (operator tptp-rec:inputs-nil 0)
    (operator tptp-rec:inputs-cons 2) (operator tptp-rec:span 2)
    (operator tptp-rec:fof 5) (operator tptp-rec:cnf 5)
    (operator tptp-rec:tff 5) (operator tptp-rec:thf 5)
    (operator tptp-rec:tcf 5) (operator tptp-rec:tpi 5)
    (operator tptp-rec:include 4)
    (operator tptp-rec:name-word 1) (operator tptp-rec:name-integer 1)
    (operator tptp-rec:name-none 0) (operator tptp-rec:role 1)
    (operator tptp-rec:word-lower 1) (operator tptp-rec:word-quoted 1)
    (operator tptp-rec:word-back 1) (operator tptp-rec:word-defined 1)
    (operator tptp-rec:word-system 1) (operator tptp-rec:word-upper 1)
    (operator tptp-rec:number-integer 1)
    (operator tptp-rec:number-rational 1)
    (operator tptp-rec:number-real 1)
    (operator tptp-rec:distinct-object 1)
    (operator tptp-rec:annotation-none 0) (operator tptp-rec:annotation 2)
    (operator tptp-rec:optional-none 0) (operator tptp-rec:optional-info 1)
    (operator tptp-rec:atom 2) (operator tptp-rec:equals 2)
    (operator tptp-rec:not-equals 2) (operator tptp-rec:not 1)
    (operator tptp-rec:and 2) (operator tptp-rec:or 2)
    (operator tptp-rec:iff 2) (operator tptp-rec:implies 2)
    (operator tptp-rec:reverse-implies 2) (operator tptp-rec:xor 2)
    (operator tptp-rec:nor 2) (operator tptp-rec:nand 2)
    (operator tptp-rec:forall 2) (operator tptp-rec:exists 2)
    (operator tptp-rec:lambda 2) (operator tptp-rec:choice 2)
    (operator tptp-rec:definite 2) (operator tptp-rec:pi 2)
    (operator tptp-rec:sigma 2) (operator tptp-rec:apply 2)
    (operator tptp-rec:typing 2) (operator tptp-rec:map-type 2)
    (operator tptp-rec:xprod 2) (operator tptp-rec:fun 2)
    (operator tptp-rec:var 1) (operator tptp-rec:typed-var 2)
    (operator tptp-rec:terms-nil 0) (operator tptp-rec:terms-cons 2)
    (operator tptp-rec:vars-nil 0) (operator tptp-rec:vars-cons 2)
    (operator tptp-rec:let 3) (operator tptp-rec:long-connective 2)
    (operator tptp-rec:prefix 2) (operator tptp-rec:gen-fun 2)
    (operator tptp-rec:gen-list 1) (operator tptp-rec:gen-colon 2)
    (operator tptp-rec:type-defined 1)
    (operator tptp-rec:include-all 0) (operator tptp-rec:include-names 1)
    (operator tptp-rec:include-star 0)
    (operator tptp-rec:names-nil 0) (operator tptp-rec:names-cons 2)
    (operator tptp-rec:inference 3) (operator tptp-rec:unknown 0)
    (operator tptp-rec:source-unknown 0) (operator tptp-rec:file-source 2)
    (operator tptp-rec:introduced 3) (operator tptp-rec:creator 3)
    (operator tptp-rec:theory 2))
  (equations)
  (rewrites
    (rule cat2 (head (metta-equation
      (tptp-print-v1:cat2 ?a ?b)
      (bnf-v1:text-append ?a ?b))) (body))
    (rule cat3 (head (metta-equation
      (tptp-print-v1:cat3 ?a ?b ?c)
      (bnf-v1:text-append ?a (bnf-v1:text-append ?b ?c)))) (body))
    (rule cat4 (head (metta-equation
      (tptp-print-v1:cat4 ?a ?b ?c ?d)
      (bnf-v1:text-append ?a
        (bnf-v1:text-append ?b (bnf-v1:text-append ?c ?d))))) (body))
    (rule cat5 (head (metta-equation
      (tptp-print-v1:cat5 ?a ?b ?c ?d ?e)
      (bnf-v1:text-append ?a
        (bnf-v1:text-append ?b
          (bnf-v1:text-append ?c (bnf-v1:text-append ?d ?e)))))) (body))
    (rule document (head (metta-equation
      (tptp-print-v1:document ?file)
      (bnf-v1:text->string (tptp-print-v1:file ?file)))) (body))
    (rule file (head (metta-equation
      (tptp-print-v1:file (tptp-rec:file ?inputs ?span))
      (tptp-print-v1:inputs ?inputs))) (body))
    (rule inputs-empty (head (metta-equation
      (tptp-print-v1:inputs (tptp-rec:inputs-nil))
      (bnf-v1:text-nil))) (body))
    (rule inputs-cons (head (metta-equation
      (tptp-print-v1:inputs (tptp-rec:inputs-cons ?head ?tail))
      (tptp-print-v1:cat3 (tptp-print-v1:input ?head)
        (bnf-v1:string->text "\n") (tptp-print-v1:inputs ?tail)))) (body))

    (rule input-fof (head (metta-equation
      (tptp-print-v1:input (tptp-rec:fof ?name ?role ?formula ?ann ?span))
      (tptp-print-v1:annotated "fof" ?name ?role ?formula ?ann))) (body))
    (rule input-cnf (head (metta-equation
      (tptp-print-v1:input (tptp-rec:cnf ?name ?role ?formula ?ann ?span))
      (tptp-print-v1:annotated-cnf ?name ?role ?formula ?ann))) (body))
    (rule input-tff (head (metta-equation
      (tptp-print-v1:input (tptp-rec:tff ?name ?role ?formula ?ann ?span))
      (tptp-print-v1:annotated "tff" ?name ?role ?formula ?ann))) (body))
    (rule input-thf (head (metta-equation
      (tptp-print-v1:input (tptp-rec:thf ?name ?role ?formula ?ann ?span))
      (tptp-print-v1:annotated "thf" ?name ?role ?formula ?ann))) (body))
    (rule input-tcf (head (metta-equation
      (tptp-print-v1:input (tptp-rec:tcf ?name ?role ?formula ?ann ?span))
      (tptp-print-v1:annotated-tcf ?name ?role ?formula ?ann))) (body))
    (rule input-tpi (head (metta-equation
      (tptp-print-v1:input (tptp-rec:tpi ?name ?role ?formula ?ann ?span))
      (tptp-print-v1:annotated "tpi" ?name ?role ?formula ?ann))) (body))
    (rule annotated (head (metta-equation
      (tptp-print-v1:annotated ?tag ?name ?role ?formula ?ann)
      (tptp-print-v1:cat5
        (bnf-v1:string->text ?tag) (bnf-v1:string->text "(")
        (tptp-print-v1:name ?name) (bnf-v1:string->text ",")
        (tptp-print-v1:cat5
          (tptp-print-v1:role ?role) (bnf-v1:string->text ",")
          (tptp-print-v1:expr ?formula) (tptp-print-v1:annotation ?ann)
          (bnf-v1:string->text ")."))))) (body))
    (rule annotated-cnf (head (metta-equation
      (tptp-print-v1:annotated-cnf ?name ?role ?formula ?ann)
      (tptp-print-v1:cat5
        (bnf-v1:string->text "cnf(") (tptp-print-v1:name ?name)
        (bnf-v1:string->text ",") (tptp-print-v1:role ?role)
        (tptp-print-v1:cat5
          (bnf-v1:string->text ",")
          (tptp-print-v1:cnf-clause ?formula)
          (tptp-print-v1:annotation ?ann)
          (bnf-v1:string->text ").") (bnf-v1:text-nil))))) (body))
    (rule cnf-clause-or (head (metta-equation
      (tptp-print-v1:cnf-clause (tptp-rec:or ?left ?right))
      (tptp-print-v1:cat3 (tptp-print-v1:cnf-clause ?left)
        (bnf-v1:string->text "|")
        (tptp-print-v1:cnf-clause ?right)))) (body))
    (rule cnf-clause-atom (head (metta-equation
      (tptp-print-v1:cnf-clause (tptp-rec:atom ?head ?args))
      (tptp-print-v1:expr (tptp-rec:atom ?head ?args)))) (body))
    (rule cnf-clause-equals (head (metta-equation
      (tptp-print-v1:cnf-clause (tptp-rec:equals ?left ?right))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?left)
        (bnf-v1:string->text "=") (tptp-print-v1:expr ?right)))) (body))
    (rule cnf-clause-not-equals (head (metta-equation
      (tptp-print-v1:cnf-clause (tptp-rec:not-equals ?left ?right))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?left)
        (bnf-v1:string->text "!=") (tptp-print-v1:expr ?right)))) (body))
    (rule cnf-clause-not (head (metta-equation
      (tptp-print-v1:cnf-clause (tptp-rec:not ?body))
      (tptp-print-v1:cat2 (bnf-v1:string->text "~")
        (tptp-print-v1:expr ?body)))) (body))
    (rule annotated-tcf (head (metta-equation
      (tptp-print-v1:annotated-tcf ?name ?role ?formula ?ann)
      (tptp-print-v1:cat5
        (bnf-v1:string->text "tcf(") (tptp-print-v1:name ?name)
        (bnf-v1:string->text ",") (tptp-print-v1:role ?role)
        (tptp-print-v1:cat5
          (bnf-v1:string->text ",")
          (tptp-print-v1:tcf-formula ?formula)
          (tptp-print-v1:annotation ?ann)
          (bnf-v1:string->text ").") (bnf-v1:text-nil))))) (body))
    (rule tcf-forall (head (metta-equation
      (tptp-print-v1:tcf-formula (tptp-rec:forall ?vars ?body))
      (tptp-print-v1:cat5 (bnf-v1:string->text "![")
        (tptp-print-v1:vars ?vars) (bnf-v1:string->text "]:(")
        (tptp-print-v1:cnf-clause ?body)
        (bnf-v1:string->text ")")))) (body))
    (rule tcf-or (head (metta-equation
      (tptp-print-v1:tcf-formula (tptp-rec:or ?left ?right))
      (tptp-print-v1:cnf-clause (tptp-rec:or ?left ?right)))) (body))
    (rule tcf-atom (head (metta-equation
      (tptp-print-v1:tcf-formula (tptp-rec:atom ?head ?args))
      (tptp-print-v1:cnf-clause (tptp-rec:atom ?head ?args)))) (body))
    (rule tcf-equals (head (metta-equation
      (tptp-print-v1:tcf-formula (tptp-rec:equals ?left ?right))
      (tptp-print-v1:cnf-clause (tptp-rec:equals ?left ?right)))) (body))
    (rule tcf-not-equals (head (metta-equation
      (tptp-print-v1:tcf-formula (tptp-rec:not-equals ?left ?right))
      (tptp-print-v1:cnf-clause (tptp-rec:not-equals ?left ?right)))) (body))
    (rule tcf-not (head (metta-equation
      (tptp-print-v1:tcf-formula (tptp-rec:not ?body))
      (tptp-print-v1:cnf-clause (tptp-rec:not ?body)))) (body))
    (rule input-include (head (metta-equation
      (tptp-print-v1:input
        (tptp-rec:include ?file ?selection ?qualification ?span))
      (tptp-print-v1:cat4 (bnf-v1:string->text "include(")
        (tptp-print-v1:word ?file)
        (tptp-print-v1:include-tail ?selection ?qualification)
        (bnf-v1:string->text ").")))) (body))
    (rule include-tail (head (metta-equation
      (tptp-print-v1:include-tail ?selection ?qualification)
      (tptp-print-v1:cat2 (tptp-print-v1:selection ?selection)
        (tptp-print-v1:qualification ?qualification)))) (body))
    (rule selection-all (head (metta-equation
      (tptp-print-v1:selection (tptp-rec:include-all))
      (bnf-v1:text-nil))) (body))
    (rule selection-star (head (metta-equation
      (tptp-print-v1:selection (tptp-rec:include-star))
      (bnf-v1:string->text ",*"))) (body))
    (rule selection-names (head (metta-equation
      (tptp-print-v1:selection (tptp-rec:include-names ?names))
      (tptp-print-v1:cat3 (bnf-v1:string->text ",[" )
        (tptp-print-v1:names ?names) (bnf-v1:string->text "]")))) (body))
    (rule qualification-none (head (metta-equation
      (tptp-print-v1:qualification (tptp-rec:name-none))
      (bnf-v1:text-nil))) (body))
    (rule qualification-word (head (metta-equation
      (tptp-print-v1:qualification (tptp-rec:name-word ?word))
      (tptp-print-v1:cat2 (bnf-v1:string->text ",")
        (tptp-print-v1:name (tptp-rec:name-word ?word))))) (body))
    (rule qualification-integer (head (metta-equation
      (tptp-print-v1:qualification (tptp-rec:name-integer ?text))
      (tptp-print-v1:cat2 (bnf-v1:string->text ",")
        (tptp-print-v1:name (tptp-rec:name-integer ?text))))) (body))

    (rule name-word (head (metta-equation
      (tptp-print-v1:name (tptp-rec:name-word ?word))
      (tptp-print-v1:word ?word))) (body))
    (rule name-integer (head (metta-equation
      (tptp-print-v1:name (tptp-rec:name-integer ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule role (head (metta-equation
      (tptp-print-v1:role (tptp-rec:role ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-lower (head (metta-equation
      (tptp-print-v1:word (tptp-rec:word-lower ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-quoted (head (metta-equation
      (tptp-print-v1:word (tptp-rec:word-quoted ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-back (head (metta-equation
      (tptp-print-v1:word (tptp-rec:word-back ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-defined (head (metta-equation
      (tptp-print-v1:word (tptp-rec:word-defined ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-system (head (metta-equation
      (tptp-print-v1:word (tptp-rec:word-system ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-upper (head (metta-equation
      (tptp-print-v1:word (tptp-rec:word-upper ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-integer (head (metta-equation
      (tptp-print-v1:word (tptp-rec:number-integer ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-rational (head (metta-equation
      (tptp-print-v1:word (tptp-rec:number-rational ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-real (head (metta-equation
      (tptp-print-v1:word (tptp-rec:number-real ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule word-distinct (head (metta-equation
      (tptp-print-v1:word (tptp-rec:distinct-object ?text))
      (bnf-v1:string->text ?text))) (body))

    (rule annotation-none (head (metta-equation
      (tptp-print-v1:annotation (tptp-rec:annotation-none))
      (bnf-v1:text-nil))) (body))
    (rule annotation (head (metta-equation
      (tptp-print-v1:annotation (tptp-rec:annotation ?source ?optional))
      (tptp-print-v1:cat3 (bnf-v1:string->text ",")
        (tptp-print-v1:expr ?source) (tptp-print-v1:optional ?optional)))) (body))
    (rule optional-none (head (metta-equation
      (tptp-print-v1:optional (tptp-rec:optional-none))
      (bnf-v1:text-nil))) (body))
    (rule optional-info (head (metta-equation
      (tptp-print-v1:optional (tptp-rec:optional-info ?info))
      (tptp-print-v1:cat2 (bnf-v1:string->text ",")
        (tptp-print-v1:expr ?info)))) (body))

    (rule terms-empty (head (metta-equation
      (tptp-print-v1:terms (tptp-rec:terms-nil))
      (bnf-v1:text-nil))) (body))
    (rule terms-one (head (metta-equation
      (tptp-print-v1:terms
        (tptp-rec:terms-cons ?head (tptp-rec:terms-nil)))
      (tptp-print-v1:expr ?head))) (body))
    (rule terms-many (head (metta-equation
      (tptp-print-v1:terms
        (tptp-rec:terms-cons ?head
          (tptp-rec:terms-cons ?next ?tail)))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?head)
        (bnf-v1:string->text ",")
        (tptp-print-v1:terms (tptp-rec:terms-cons ?next ?tail))))) (body))
    (rule vars-empty (head (metta-equation
      (tptp-print-v1:vars (tptp-rec:vars-nil))
      (bnf-v1:text-nil))) (body))
    (rule vars-one (head (metta-equation
      (tptp-print-v1:vars
        (tptp-rec:vars-cons ?head (tptp-rec:vars-nil)))
      (tptp-print-v1:expr ?head))) (body))
    (rule vars-many (head (metta-equation
      (tptp-print-v1:vars
        (tptp-rec:vars-cons ?head (tptp-rec:vars-cons ?next ?tail)))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?head)
        (bnf-v1:string->text ",")
        (tptp-print-v1:vars (tptp-rec:vars-cons ?next ?tail))))) (body))
    (rule names-empty (head (metta-equation
      (tptp-print-v1:names (tptp-rec:names-nil))
      (bnf-v1:text-nil))) (body))
    (rule names-one (head (metta-equation
      (tptp-print-v1:names
        (tptp-rec:names-cons ?head (tptp-rec:names-nil)))
      (tptp-print-v1:name ?head))) (body))
    (rule names-many (head (metta-equation
      (tptp-print-v1:names
        (tptp-rec:names-cons ?head (tptp-rec:names-cons ?next ?tail)))
      (tptp-print-v1:cat3 (tptp-print-v1:name ?head)
        (bnf-v1:string->text ",")
        (tptp-print-v1:names (tptp-rec:names-cons ?next ?tail))))) (body))

    (rule call-empty (head (metta-equation
      (tptp-print-v1:call ?head (tptp-rec:terms-nil))
      (tptp-print-v1:word ?head))) (body))
    (rule call-args (head (metta-equation
      (tptp-print-v1:call ?head (tptp-rec:terms-cons ?first ?rest))
      (tptp-print-v1:cat4 (tptp-print-v1:word ?head)
        (bnf-v1:string->text "(")
        (tptp-print-v1:terms (tptp-rec:terms-cons ?first ?rest))
        (bnf-v1:string->text ")")))) (body))
    (rule brackets (head (metta-equation
      (tptp-print-v1:brackets ?terms)
      (tptp-print-v1:cat3 (bnf-v1:string->text "[")
        (tptp-print-v1:terms ?terms) (bnf-v1:string->text "]")))) (body))
    (rule binary (head (metta-equation
      (tptp-print-v1:binary ?operator ?left ?right)
      (tptp-print-v1:cat5 (bnf-v1:string->text "(")
        (tptp-print-v1:expr ?left) (bnf-v1:string->text ?operator)
        (tptp-print-v1:expr ?right) (bnf-v1:string->text ")")))) (body))
    (rule quant (head (metta-equation
      (tptp-print-v1:quant ?operator ?variables ?body)
      (tptp-print-v1:cat5 (bnf-v1:string->text ?operator)
        (bnf-v1:string->text "[") (tptp-print-v1:vars ?variables)
        (bnf-v1:string->text "]:")
        (tptp-print-v1:cat3 (bnf-v1:string->text "(")
          (tptp-print-v1:expr ?body) (bnf-v1:string->text ")"))))) (body))

    (rule type-defined (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:type-defined ?name))
      (tptp-print-v1:word ?name))) (body))
    (rule type-word-lower (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:word-lower ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule type-word-upper (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:word-upper ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule type-word-defined (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:word-defined ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule type-word-system (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:word-system ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule type-word-quoted (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:word-quoted ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule type-word-back (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:word-back ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule type-var (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:var ?name))
      (tptp-print-v1:word ?name))) (body))
    (rule type-atom (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:atom ?head ?args))
      (tptp-print-v1:call ?head ?args))) (body))
    (rule type-fun (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:fun ?head ?args))
      (tptp-print-v1:call ?head ?args))) (body))
    (rule type-tuple (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:terms-cons ?head ?tail))
      (tptp-print-v1:brackets
        (tptp-rec:terms-cons ?head ?tail)))) (body))
    (rule type-map (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:map-type ?left ?right))
      (tptp-print-v1:cat5 (bnf-v1:string->text "(")
        (tptp-print-v1:type-expr ?left) (bnf-v1:string->text ">")
        (tptp-print-v1:type-expr ?right) (bnf-v1:string->text ")")))) (body))
    (rule type-xprod (head (metta-equation
      (tptp-print-v1:type-expr (tptp-rec:xprod ?left ?right))
      (tptp-print-v1:cat5 (bnf-v1:string->text "(")
        (tptp-print-v1:type-expr ?left) (bnf-v1:string->text "*")
        (tptp-print-v1:type-expr ?right) (bnf-v1:string->text ")")))) (body))

    (rule let-types-one (head (metta-equation
      (tptp-print-v1:let-types (tptp-rec:typing ?name ?type))
      (tptp-print-v1:expr (tptp-rec:typing ?name ?type)))) (body))
    (rule let-types-many (head (metta-equation
      (tptp-print-v1:let-types (tptp-rec:terms-cons ?head ?tail))
      (tptp-print-v1:brackets
        (tptp-rec:terms-cons ?head ?tail)))) (body))
    (rule let-definitions-one (head (metta-equation
      (tptp-print-v1:let-definitions (tptp-rec:equals ?left ?right))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?left)
        (bnf-v1:string->text ":=") (tptp-print-v1:expr ?right)))) (body))
    (rule let-definitions-many (head (metta-equation
      (tptp-print-v1:let-definitions
        (tptp-rec:terms-cons ?head ?tail))
      (tptp-print-v1:cat3 (bnf-v1:string->text "[")
        (tptp-print-v1:let-definition-items
          (tptp-rec:terms-cons ?head ?tail))
        (bnf-v1:string->text "]")))) (body))
    (rule let-definition-items-one (head (metta-equation
      (tptp-print-v1:let-definition-items
        (tptp-rec:terms-cons
          (tptp-rec:equals ?left ?right) (tptp-rec:terms-nil)))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?left)
        (bnf-v1:string->text ":=") (tptp-print-v1:expr ?right)))) (body))
    (rule let-definition-items-many (head (metta-equation
      (tptp-print-v1:let-definition-items
        (tptp-rec:terms-cons (tptp-rec:equals ?left ?right)
          (tptp-rec:terms-cons ?next ?tail)))
      (tptp-print-v1:cat3
        (tptp-print-v1:cat3 (tptp-print-v1:expr ?left)
          (bnf-v1:string->text ":=") (tptp-print-v1:expr ?right))
        (bnf-v1:string->text ",")
        (tptp-print-v1:let-definition-items
          (tptp-rec:terms-cons ?next ?tail))))) (body))

    (rule expr-atom (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:atom ?head ?args))
      (tptp-print-v1:call ?head ?args))) (body))
    (rule expr-fun (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:fun ?head ?args))
      (tptp-print-v1:call ?head ?args))) (body))
    (rule expr-var (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:var ?name))
      (tptp-print-v1:word ?name))) (body))
    (rule expr-name-word (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:name-word ?word))
      (tptp-print-v1:word ?word))) (body))
    (rule expr-name-integer (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:name-integer ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-number-integer (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:number-integer ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-number-rational (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:number-rational ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-number-real (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:number-real ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-distinct-object (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:distinct-object ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-typed-var (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:typed-var ?variable ?type))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?variable)
        (bnf-v1:string->text ":") (tptp-print-v1:expr ?type)))) (body))
    (rule expr-word-lower (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:word-lower ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-word-quoted (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:word-quoted ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-word-back (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:word-back ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-word-defined (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:word-defined ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-word-system (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:word-system ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-word-upper (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:word-upper ?text))
      (bnf-v1:string->text ?text))) (body))
    (rule expr-defined-type (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:type-defined ?name))
      (tptp-print-v1:word ?name))) (body))
    (rule expr-terms-empty (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:terms-nil))
      (bnf-v1:string->text "()"))) (body))
    (rule expr-terms-cons (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:terms-cons ?head ?tail))
      (tptp-print-v1:cat3 (bnf-v1:string->text "(")
        (tptp-print-v1:terms (tptp-rec:terms-cons ?head ?tail))
        (bnf-v1:string->text ")")))) (body))
    (rule expr-equals (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:equals ?left ?right))
      (tptp-print-v1:binary "=" ?left ?right))) (body))
    (rule expr-not-equals (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:not-equals ?left ?right))
      (tptp-print-v1:binary "!=" ?left ?right))) (body))
    (rule expr-and (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:and ?left ?right))
      (tptp-print-v1:binary "&" ?left ?right))) (body))
    (rule expr-or (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:or ?left ?right))
      (tptp-print-v1:binary "|" ?left ?right))) (body))
    (rule expr-iff (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:iff ?left ?right))
      (tptp-print-v1:binary "<=>" ?left ?right))) (body))
    (rule expr-implies (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:implies ?left ?right))
      (tptp-print-v1:binary "=>" ?left ?right))) (body))
    (rule expr-reverse-implies (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:reverse-implies ?left ?right))
      (tptp-print-v1:binary "<=" ?left ?right))) (body))
    (rule expr-xor (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:xor ?left ?right))
      (tptp-print-v1:binary "<~>" ?left ?right))) (body))
    (rule expr-nor (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:nor ?left ?right))
      (tptp-print-v1:binary "~|" ?left ?right))) (body))
    (rule expr-nand (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:nand ?left ?right))
      (tptp-print-v1:binary "~&" ?left ?right))) (body))
    (rule expr-apply (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:apply ?left ?right))
      (tptp-print-v1:binary "@" ?left ?right))) (body))
    (rule expr-typing (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:typing ?left ?right))
      (tptp-print-v1:cat5 (bnf-v1:string->text "(")
        (tptp-print-v1:expr ?left) (bnf-v1:string->text ":")
        (tptp-print-v1:type-expr ?right)
        (bnf-v1:string->text ")")))) (body))
    (rule expr-map-type (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:map-type ?left ?right))
      (tptp-print-v1:type-expr (tptp-rec:map-type ?left ?right)))) (body))
    (rule expr-xprod (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:xprod ?left ?right))
      (tptp-print-v1:type-expr (tptp-rec:xprod ?left ?right)))) (body))
    (rule expr-not (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:not ?body))
      (tptp-print-v1:cat3 (bnf-v1:string->text "(~")
        (tptp-print-v1:expr ?body) (bnf-v1:string->text ")")))) (body))
    (rule expr-forall (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:forall ?vars ?body))
      (tptp-print-v1:quant "!" ?vars ?body))) (body))
    (rule expr-exists (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:exists ?vars ?body))
      (tptp-print-v1:quant "?" ?vars ?body))) (body))
    (rule expr-lambda (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:lambda ?vars ?body))
      (tptp-print-v1:quant "^" ?vars ?body))) (body))
    (rule expr-choice (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:choice ?vars ?body))
      (tptp-print-v1:quant "@+" ?vars ?body))) (body))
    (rule expr-definite (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:definite ?vars ?body))
      (tptp-print-v1:quant "@-" ?vars ?body))) (body))
    (rule expr-pi (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:pi ?vars ?body))
      (tptp-print-v1:quant "!>" ?vars ?body))) (body))
    (rule expr-sigma (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:sigma ?vars ?body))
      (tptp-print-v1:quant "?*" ?vars ?body))) (body))
    (rule expr-let (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:let ?types ?definitions ?body))
      (tptp-print-v1:cat5 (bnf-v1:string->text "$let(")
        (tptp-print-v1:let-types ?types) (bnf-v1:string->text ",")
        (tptp-print-v1:let-definitions ?definitions)
        (tptp-print-v1:cat3 (bnf-v1:string->text ",")
          (tptp-print-v1:expr ?body) (bnf-v1:string->text ")"))))) (body))
    (rule expr-long-empty (head (metta-equation
      (tptp-print-v1:expr
        (tptp-rec:long-connective ?name (tptp-rec:terms-nil)))
      (tptp-print-v1:cat3 (bnf-v1:string->text "{")
        (tptp-print-v1:expr ?name) (bnf-v1:string->text "}")))) (body))
    (rule expr-long-args (head (metta-equation
      (tptp-print-v1:expr
        (tptp-rec:long-connective ?name
          (tptp-rec:terms-cons ?first ?rest)))
      (tptp-print-v1:cat5 (bnf-v1:string->text "{")
        (tptp-print-v1:expr ?name) (bnf-v1:string->text "(")
        (tptp-print-v1:terms (tptp-rec:terms-cons ?first ?rest))
        (bnf-v1:string->text ")}")))) (body))
    (rule expr-prefix (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:prefix ?operator ?body))
      (tptp-print-v1:cat4 (tptp-print-v1:expr ?operator)
        (bnf-v1:string->text "(") (tptp-print-v1:expr ?body)
        (bnf-v1:string->text ")")))) (body))
    (rule expr-gen-fun (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:gen-fun ?head ?args))
      (tptp-print-v1:call ?head ?args))) (body))
    (rule expr-gen-list (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:gen-list ?items))
      (tptp-print-v1:brackets ?items))) (body))
    (rule expr-gen-colon (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:gen-colon ?left ?right))
      (tptp-print-v1:cat3 (tptp-print-v1:expr ?left)
        (bnf-v1:string->text ":") (tptp-print-v1:expr ?right)))) (body))
    (rule expr-inference (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:inference ?rule ?info ?parents))
      (tptp-print-v1:cat5 (bnf-v1:string->text "inference(")
        (tptp-print-v1:expr ?rule) (bnf-v1:string->text ",")
        (tptp-print-v1:expr ?info)
        (tptp-print-v1:cat3 (bnf-v1:string->text ",")
          (tptp-print-v1:expr ?parents) (bnf-v1:string->text ")"))))) (body))
    (rule expr-file-source-none (head (metta-equation
      (tptp-print-v1:expr
        (tptp-rec:file-source ?file (tptp-rec:name-none)))
      (tptp-print-v1:cat3 (bnf-v1:string->text "file(")
        (tptp-print-v1:word ?file) (bnf-v1:string->text ")")))) (body))
    (rule expr-file-source-word (head (metta-equation
      (tptp-print-v1:expr
        (tptp-rec:file-source ?file (tptp-rec:name-word ?word)))
      (tptp-print-v1:cat5 (bnf-v1:string->text "file(")
        (tptp-print-v1:word ?file) (bnf-v1:string->text ",")
        (tptp-print-v1:name (tptp-rec:name-word ?word))
        (bnf-v1:string->text ")")))) (body))
    (rule expr-file-source-integer (head (metta-equation
      (tptp-print-v1:expr
        (tptp-rec:file-source ?file (tptp-rec:name-integer ?text)))
      (tptp-print-v1:cat5 (bnf-v1:string->text "file(")
        (tptp-print-v1:word ?file) (bnf-v1:string->text ",")
        (tptp-print-v1:name (tptp-rec:name-integer ?text))
        (bnf-v1:string->text ")")))) (body))
    (rule expr-introduced (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:introduced ?type ?info ?parents))
      (tptp-print-v1:cat5 (bnf-v1:string->text "introduced(")
        (tptp-print-v1:expr ?type) (bnf-v1:string->text ",")
        (tptp-print-v1:expr ?info)
        (tptp-print-v1:cat3 (bnf-v1:string->text ",")
          (tptp-print-v1:expr ?parents) (bnf-v1:string->text ")"))))) (body))
    (rule expr-creator (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:creator ?name ?info ?parents))
      (tptp-print-v1:cat5 (bnf-v1:string->text "creator(")
        (tptp-print-v1:expr ?name) (bnf-v1:string->text ",")
        (tptp-print-v1:expr ?info)
        (tptp-print-v1:cat3 (bnf-v1:string->text ",")
          (tptp-print-v1:expr ?parents) (bnf-v1:string->text ")"))))) (body))
    (rule expr-theory-none (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:theory ?name (tptp-rec:optional-none)))
      (tptp-print-v1:cat3 (bnf-v1:string->text "theory(")
        (tptp-print-v1:expr ?name) (bnf-v1:string->text ")")))) (body))
    (rule expr-theory-info (head (metta-equation
      (tptp-print-v1:expr
        (tptp-rec:theory ?name (tptp-rec:optional-info ?info)))
      (tptp-print-v1:cat5 (bnf-v1:string->text "theory(")
        (tptp-print-v1:expr ?name) (bnf-v1:string->text ",")
        (tptp-print-v1:expr ?info) (bnf-v1:string->text ")")))) (body))
    (rule expr-unknown (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:unknown))
      (bnf-v1:string->text "unknown"))) (body))
    (rule expr-source-unknown (head (metta-equation
      (tptp-print-v1:expr (tptp-rec:source-unknown))
      (bnf-v1:string->text "unknown"))) (body))))
