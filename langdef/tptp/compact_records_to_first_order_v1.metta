; First-order views of source-shaped compact TPTP records.
;
; The source digest and zero-based input position define occurrence identity.
; Formula variables are scoped by that occurrence. Source spans, annotations,
; include selections, and include qualifiers remain explicit in the result.
(gslt-presentation-v1 TptpCompactRecordsToFirstOrderV1
  (signature
    (operator metta-equation 2) (operator + 2)
    (operator tptp-lex-v1:word-value 1) (operator TPTP:LexicalValue 1)
    (operator TPTP:SourceDocument 4)

    (operator tptp-view-v1:first-order-document 1)
    (operator tptp-view-v1:clause-data 1)
    (operator tptp-view-v1:fo-inputs 3) (operator tptp-view-v1:fo-input 3)
    (operator tptp-view-v1:cnf-clauses 3) (operator tptp-view-v1:cnf-clause 3)
    (operator tptp-view-v1:fo-name 1) (operator tptp-view-v1:cnf-name 1)
    (operator tptp-view-v1:fo-quoted-name 1)
    (operator tptp-view-v1:cnf-quoted-name 1)
    (operator tptp-view-v1:fo-role 1) (operator tptp-view-v1:cnf-role 1)
    (operator tptp-view-v1:fo-symbol 1) (operator tptp-view-v1:cnf-symbol 1)
    (operator tptp-view-v1:fo-lower-symbol 1)
    (operator tptp-view-v1:fo-quoted-symbol 1)
    (operator tptp-view-v1:cnf-lower-symbol 1)
    (operator tptp-view-v1:cnf-quoted-symbol 1)
    (operator tptp-view-v1:fo-term 2) (operator tptp-view-v1:fo-terms 2)
    (operator tptp-view-v1:cnf-term 2) (operator tptp-view-v1:cnf-terms 2)
    (operator tptp-view-v1:fo-atom 3) (operator tptp-view-v1:cnf-atom 3)
    (operator tptp-view-v1:fo-formula 2) (operator tptp-view-v1:fo-vars 2)
    (operator tptp-view-v1:cnf-literals 2)
    (operator tptp-view-v1:cnf-literals-append 2)
    (operator tptp-view-v1:fo-span 1) (operator tptp-view-v1:cnf-span 1)
    (operator tptp-view-v1:fo-annotation 1)
    (operator tptp-view-v1:cnf-annotation 1)
    (operator tptp-view-v1:fo-syntax 1)
    (operator tptp-view-v1:fo-syntax-nodes 1)
    (operator tptp-view-v1:cnf-syntax 1)
    (operator tptp-view-v1:cnf-syntax-nodes 1)
    (operator tptp-view-v1:fo-selection 1)
    (operator tptp-view-v1:fo-names 1)
    (operator tptp-view-v1:fo-qualifier 1)
    (operator tptp-view-v1:fo-include-path 1)
    (operator tptp-view-v1:syntax 1)
    (operator tptp-view-v1:tag0 1) (operator tptp-view-v1:tag1 2)
    (operator tptp-view-v1:tag2 3) (operator tptp-view-v1:tag3 4)
    (operator tptp-view-v1:syntax-atom 1)
    (operator tptp-view-v1:syntax-string 1)
    (operator tptp-view-v1:syntax-list 1)
    (operator tptp-view-v1:syntax-nodes-nil 0)
    (operator tptp-view-v1:syntax-nodes-cons 2)

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
    (operator tptp-rec:number-rational 1) (operator tptp-rec:number-real 1)
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
    (operator tptp-rec:apply 2) (operator tptp-rec:typing 2)
    (operator tptp-rec:map-type 2) (operator tptp-rec:xprod 2)
    (operator tptp-rec:fun 2) (operator tptp-rec:var 1)
    (operator tptp-rec:terms-nil 0) (operator tptp-rec:terms-cons 2)
    (operator tptp-rec:vars-nil 0) (operator tptp-rec:vars-cons 2)
    (operator tptp-rec:typed-var 2)
    (operator tptp-rec:include-all 0) (operator tptp-rec:include-names 1)
    (operator tptp-rec:include-star 0)
    (operator tptp-rec:names-nil 0) (operator tptp-rec:names-cons 2)
    (operator tptp-rec:inference 3) (operator tptp-rec:unknown 0)
    (operator tptp-rec:source-unknown 0)
    (operator tptp-rec:file-source 2) (operator tptp-rec:introduced 3)
    (operator tptp-rec:creator 3) (operator tptp-rec:theory 2)
    (operator tptp-rec:let 3) (operator tptp-rec:long-connective 2)
    (operator tptp-rec:prefix 2) (operator tptp-rec:gen-fun 2)
    (operator tptp-rec:gen-list 1) (operator tptp-rec:gen-colon 2)
    (operator tptp-rec:type-defined 1)
    (operator tptp-rec:pi 2) (operator tptp-rec:sigma 2)
    (operator tptp-rec:lambda 2) (operator tptp-rec:choice 2)
    (operator tptp-rec:definite 2)

    (operator tptp-fo:source-digest 1) (operator tptp-fo:occurrence 2)
    (operator tptp-fo:formula-name-atomic 1)
    (operator tptp-fo:formula-name-integer 1)
    (operator tptp-fo:formula-names-nil 0)
    (operator tptp-fo:formula-names-cons 2)
    (operator tptp-fo:formula-name-none 0)
    (operator tptp-fo:formula-name-some 1)
    (operator tptp-fo:role 2)
    (operator tptp-fo:dialect-fof 0) (operator tptp-fo:dialect-cnf 0)
    (operator tptp-fo:symbol-lower 0) (operator tptp-fo:symbol-quoted 0)
    (operator tptp-fo:symbol-defined 0) (operator tptp-fo:symbol-system 0)
    (operator tptp-fo:symbol-name 2)
    (operator tptp-fo:variable-name 1) (operator tptp-fo:variable-id 2)
    (operator tptp-fo:variables-nil 0) (operator tptp-fo:variables-cons 2)
    (operator tptp-fo:number-integer 0) (operator tptp-fo:number-rational 0)
    (operator tptp-fo:number-real 0)
    (operator tptp-fo:term-variable 1) (operator tptp-fo:term-function 2)
    (operator tptp-fo:term-number 2) (operator tptp-fo:term-distinct 1)
    (operator tptp-fo:terms-nil 0) (operator tptp-fo:terms-cons 2)
    (operator tptp-fo:atom-predicate 2) (operator tptp-fo:atom-equality 2)
    (operator tptp-fo:formula-atom 1) (operator tptp-fo:formula-not 1)
    (operator tptp-fo:formula-and 2) (operator tptp-fo:formula-or 2)
    (operator tptp-fo:formula-iff 2) (operator tptp-fo:formula-implies 2)
    (operator tptp-fo:formula-reverse-implies 2)
    (operator tptp-fo:formula-xor 2) (operator tptp-fo:formula-nor 2)
    (operator tptp-fo:formula-nand 2)
    (operator tptp-fo:formula-forall 2) (operator tptp-fo:formula-exists 2)
    (operator tptp-fo:syntax-atom 1) (operator tptp-fo:syntax-string 1)
    (operator tptp-fo:syntax-list 1)
    (operator tptp-fo:syntax-nodes-nil 0)
    (operator tptp-fo:syntax-nodes-cons 2)
    (operator tptp-fo:annotation-none 0) (operator tptp-fo:annotation-tree 1)
    (operator tptp-fo:source-span 2)
    (operator tptp-fo:include-all 0) (operator tptp-fo:include-names 1)
    (operator tptp-fo:formula-input 7) (operator tptp-fo:include-input 5)
    (operator tptp-fo:inputs-nil 0) (operator tptp-fo:inputs-cons 2)
    (operator tptp-fo:document 2)

    (operator fo-cnf:source-digest 1) (operator fo-cnf:occurrence 2)
    (operator fo-cnf:formula-name-atomic 1)
    (operator fo-cnf:formula-name-integer 1) (operator fo-cnf:role 2)
    (operator fo-cnf:symbol-lower 0) (operator fo-cnf:symbol-quoted 0)
    (operator fo-cnf:symbol-defined 0) (operator fo-cnf:symbol-system 0)
    (operator fo-cnf:symbol-name 2)
    (operator fo-cnf:variable-name 1) (operator fo-cnf:variable-id 2)
    (operator fo-cnf:number-integer 0) (operator fo-cnf:number-rational 0)
    (operator fo-cnf:number-real 0)
    (operator fo-cnf:term-variable 1) (operator fo-cnf:term-function 2)
    (operator fo-cnf:term-number 2) (operator fo-cnf:term-distinct 1)
    (operator fo-cnf:terms-nil 0) (operator fo-cnf:terms-cons 2)
    (operator fo-cnf:atom-predicate 2) (operator fo-cnf:atom-equality 2)
    (operator fo-cnf:literal-positive 1) (operator fo-cnf:literal-negative 1)
    (operator fo-cnf:literals-nil 0) (operator fo-cnf:literals-cons 2)
    (operator fo-cnf:syntax-atom 1) (operator fo-cnf:syntax-string 1)
    (operator fo-cnf:syntax-list 1)
    (operator fo-cnf:syntax-nodes-nil 0)
    (operator fo-cnf:syntax-nodes-cons 2)
    (operator fo-cnf:annotation-none 0) (operator fo-cnf:annotation-tree 1)
    (operator fo-cnf:source-span 2) (operator fo-cnf:clause 6)
    (operator fo-cnf:clauses-nil 0) (operator fo-cnf:clauses-cons 2)
    (operator fo-cnf:problem 2))
  (equations)
  (rewrites
    (rule first-order-document (head (metta-equation
      (tptp-view-v1:first-order-document
        (TPTP:SourceDocument ?identity ?digest ?text
          (tptp-rec:file ?inputs ?file-span)))
      (tptp-fo:document (tptp-fo:source-digest ?digest)
        (tptp-view-v1:fo-inputs ?inputs ?digest 0)))) (body))
    (rule clause-data (head (metta-equation
      (tptp-view-v1:clause-data
        (TPTP:SourceDocument ?identity ?digest ?text
          (tptp-rec:file ?inputs ?file-span)))
      (fo-cnf:problem (fo-cnf:source-digest ?digest)
        (tptp-view-v1:cnf-clauses ?inputs ?digest 0)))) (body))

    (rule fo-inputs-nil (head (metta-equation
      (tptp-view-v1:fo-inputs (tptp-rec:inputs-nil) ?digest ?index)
      (tptp-fo:inputs-nil))) (body))
    (rule fo-inputs-fof (head (metta-equation
      (tptp-view-v1:fo-inputs
        (tptp-rec:inputs-cons
          (tptp-rec:fof ?name ?role ?formula ?annotation ?span) ?tail)
        ?digest ?index)
      (tptp-fo:inputs-cons
        (tptp-view-v1:fo-input
          (tptp-rec:fof ?name ?role ?formula ?annotation ?span) ?digest ?index)
        (tptp-view-v1:fo-inputs ?tail ?digest (+ ?index 1))))) (body))
    (rule fo-inputs-cnf (head (metta-equation
      (tptp-view-v1:fo-inputs
        (tptp-rec:inputs-cons
          (tptp-rec:cnf ?name ?role ?formula ?annotation ?span) ?tail)
        ?digest ?index)
      (tptp-fo:inputs-cons
        (tptp-view-v1:fo-input
          (tptp-rec:cnf ?name ?role ?formula ?annotation ?span) ?digest ?index)
        (tptp-view-v1:fo-inputs ?tail ?digest (+ ?index 1))))) (body))
    (rule fo-inputs-include (head (metta-equation
      (tptp-view-v1:fo-inputs
        (tptp-rec:inputs-cons
          (tptp-rec:include ?file ?selection ?qualifier ?span) ?tail)
        ?digest ?index)
      (tptp-fo:inputs-cons
        (tptp-view-v1:fo-input
          (tptp-rec:include ?file ?selection ?qualifier ?span) ?digest ?index)
        (tptp-view-v1:fo-inputs ?tail ?digest (+ ?index 1))))) (body))
    (rule fo-inputs-skip-tff (head (metta-equation
      (tptp-view-v1:fo-inputs
        (tptp-rec:inputs-cons (tptp-rec:tff ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:fo-inputs ?tail ?digest (+ ?index 1)))) (body))
    (rule fo-inputs-skip-thf (head (metta-equation
      (tptp-view-v1:fo-inputs
        (tptp-rec:inputs-cons (tptp-rec:thf ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:fo-inputs ?tail ?digest (+ ?index 1)))) (body))
    (rule fo-inputs-skip-tcf (head (metta-equation
      (tptp-view-v1:fo-inputs
        (tptp-rec:inputs-cons (tptp-rec:tcf ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:fo-inputs ?tail ?digest (+ ?index 1)))) (body))
    (rule fo-inputs-skip-tpi (head (metta-equation
      (tptp-view-v1:fo-inputs
        (tptp-rec:inputs-cons (tptp-rec:tpi ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:fo-inputs ?tail ?digest (+ ?index 1)))) (body))

    (rule fo-input-fof (head (metta-equation
      (tptp-view-v1:fo-input
        (tptp-rec:fof ?name ?role ?formula ?annotation ?span) ?digest ?index)
      (tptp-fo:formula-input
        (tptp-fo:occurrence (tptp-fo:source-digest ?digest) ?index)
        (tptp-fo:dialect-fof) (tptp-view-v1:fo-name ?name)
        (tptp-view-v1:fo-role ?role)
        (tptp-view-v1:fo-formula ?formula
          (tptp-fo:occurrence (tptp-fo:source-digest ?digest) ?index))
        (tptp-view-v1:fo-annotation ?annotation)
        (tptp-view-v1:fo-span ?span)))) (body))
    (rule fo-input-cnf (head (metta-equation
      (tptp-view-v1:fo-input
        (tptp-rec:cnf ?name ?role ?formula ?annotation ?span) ?digest ?index)
      (tptp-fo:formula-input
        (tptp-fo:occurrence (tptp-fo:source-digest ?digest) ?index)
        (tptp-fo:dialect-cnf) (tptp-view-v1:fo-name ?name)
        (tptp-view-v1:fo-role ?role)
        (tptp-view-v1:fo-formula ?formula
          (tptp-fo:occurrence (tptp-fo:source-digest ?digest) ?index))
        (tptp-view-v1:fo-annotation ?annotation)
        (tptp-view-v1:fo-span ?span)))) (body))
    (rule fo-input-include (head (metta-equation
      (tptp-view-v1:fo-input
        (tptp-rec:include ?file ?selection ?qualifier ?span) ?digest ?index)
      (tptp-fo:include-input
        (tptp-fo:occurrence (tptp-fo:source-digest ?digest) ?index)
        (tptp-view-v1:fo-include-path ?file)
        (tptp-view-v1:fo-selection ?selection)
        (tptp-view-v1:fo-qualifier ?qualifier)
        (tptp-view-v1:fo-span ?span)))) (body))

    (rule cnf-clauses-nil (head (metta-equation
      (tptp-view-v1:cnf-clauses (tptp-rec:inputs-nil) ?digest ?index)
      (fo-cnf:clauses-nil))) (body))
    (rule cnf-clauses-cons (head (metta-equation
      (tptp-view-v1:cnf-clauses
        (tptp-rec:inputs-cons
          (tptp-rec:cnf ?name ?role ?formula ?annotation ?span) ?tail)
        ?digest ?index)
      (fo-cnf:clauses-cons
        (tptp-view-v1:cnf-clause
          (tptp-rec:cnf ?name ?role ?formula ?annotation ?span) ?digest ?index)
        (tptp-view-v1:cnf-clauses ?tail ?digest (+ ?index 1))))) (body))
    (rule cnf-clauses-skip-fof (head (metta-equation
      (tptp-view-v1:cnf-clauses
        (tptp-rec:inputs-cons (tptp-rec:fof ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:cnf-clauses ?tail ?digest (+ ?index 1)))) (body))
    (rule cnf-clauses-skip-tff (head (metta-equation
      (tptp-view-v1:cnf-clauses
        (tptp-rec:inputs-cons (tptp-rec:tff ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:cnf-clauses ?tail ?digest (+ ?index 1)))) (body))
    (rule cnf-clauses-skip-thf (head (metta-equation
      (tptp-view-v1:cnf-clauses
        (tptp-rec:inputs-cons (tptp-rec:thf ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:cnf-clauses ?tail ?digest (+ ?index 1)))) (body))
    (rule cnf-clauses-skip-tcf (head (metta-equation
      (tptp-view-v1:cnf-clauses
        (tptp-rec:inputs-cons (tptp-rec:tcf ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:cnf-clauses ?tail ?digest (+ ?index 1)))) (body))
    (rule cnf-clauses-skip-tpi (head (metta-equation
      (tptp-view-v1:cnf-clauses
        (tptp-rec:inputs-cons (tptp-rec:tpi ?a ?b ?c ?d ?e) ?tail)
        ?digest ?index)
      (tptp-view-v1:cnf-clauses ?tail ?digest (+ ?index 1)))) (body))
    (rule cnf-clauses-skip-include (head (metta-equation
      (tptp-view-v1:cnf-clauses
        (tptp-rec:inputs-cons (tptp-rec:include ?a ?b ?c ?d) ?tail)
        ?digest ?index)
      (tptp-view-v1:cnf-clauses ?tail ?digest (+ ?index 1)))) (body))
    (rule cnf-clause (head (metta-equation
      (tptp-view-v1:cnf-clause
        (tptp-rec:cnf ?name ?role ?formula ?annotation ?span) ?digest ?index)
      (fo-cnf:clause
        (fo-cnf:occurrence (fo-cnf:source-digest ?digest) ?index)
        (tptp-view-v1:cnf-name ?name) (tptp-view-v1:cnf-role ?role)
        (tptp-view-v1:cnf-literals ?formula
          (fo-cnf:occurrence (fo-cnf:source-digest ?digest) ?index))
        (tptp-view-v1:cnf-annotation ?annotation)
        (tptp-view-v1:cnf-span ?span)))) (body))

    (rule fo-name-lower (head (metta-equation
      (tptp-view-v1:fo-name
        (tptp-rec:name-word (tptp-rec:word-lower ?raw)))
      (tptp-fo:formula-name-atomic
        (tptp-fo:symbol-name (tptp-fo:symbol-lower) ?raw)))) (body))
    (rule fo-name-quoted (head (metta-equation
      (tptp-view-v1:fo-name
        (tptp-rec:name-word (tptp-rec:word-quoted ?raw)))
      (tptp-view-v1:fo-quoted-name
        (tptp-lex-v1:word-value (tptp-rec:word-quoted ?raw))))) (body))
    (rule fo-quoted-name (head (metta-equation
      (tptp-view-v1:fo-quoted-name (TPTP:LexicalValue ?value))
      (tptp-fo:formula-name-atomic
        (tptp-fo:symbol-name (tptp-fo:symbol-quoted) ?value)))) (body))
    (rule fo-name-integer (head (metta-equation
      (tptp-view-v1:fo-name (tptp-rec:name-integer ?raw))
      (tptp-fo:formula-name-integer ?raw))) (body))
    (rule cnf-name-lower (head (metta-equation
      (tptp-view-v1:cnf-name
        (tptp-rec:name-word (tptp-rec:word-lower ?raw)))
      (fo-cnf:formula-name-atomic
        (fo-cnf:symbol-name (fo-cnf:symbol-lower) ?raw)))) (body))
    (rule cnf-name-quoted (head (metta-equation
      (tptp-view-v1:cnf-name
        (tptp-rec:name-word (tptp-rec:word-quoted ?raw)))
      (tptp-view-v1:cnf-quoted-name
        (tptp-lex-v1:word-value (tptp-rec:word-quoted ?raw))))) (body))
    (rule cnf-quoted-name (head (metta-equation
      (tptp-view-v1:cnf-quoted-name (TPTP:LexicalValue ?value))
      (fo-cnf:formula-name-atomic
        (fo-cnf:symbol-name (fo-cnf:symbol-quoted) ?value)))) (body))
    (rule cnf-name-integer (head (metta-equation
      (tptp-view-v1:cnf-name (tptp-rec:name-integer ?raw))
      (fo-cnf:formula-name-integer ?raw))) (body))
    (rule fo-role (head (metta-equation
      (tptp-view-v1:fo-role (tptp-rec:role ?raw))
      (tptp-fo:role ?raw (tptp-fo:annotation-none)))) (body))
    (rule cnf-role (head (metta-equation
      (tptp-view-v1:cnf-role (tptp-rec:role ?raw))
      (fo-cnf:role ?raw (fo-cnf:annotation-none)))) (body))

    (rule fo-symbol-lower (head (metta-equation
      (tptp-view-v1:fo-symbol (tptp-rec:word-lower ?raw))
      (tptp-view-v1:fo-lower-symbol
        (tptp-lex-v1:word-value (tptp-rec:word-lower ?raw))))) (body))
    (rule fo-lower-symbol (head (metta-equation
      (tptp-view-v1:fo-lower-symbol (TPTP:LexicalValue ?value))
      (tptp-fo:symbol-name (tptp-fo:symbol-lower) ?value))) (body))
    (rule fo-symbol-quoted (head (metta-equation
      (tptp-view-v1:fo-symbol (tptp-rec:word-quoted ?raw))
      (tptp-view-v1:fo-quoted-symbol
        (tptp-lex-v1:word-value (tptp-rec:word-quoted ?raw))))) (body))
    (rule fo-quoted-symbol (head (metta-equation
      (tptp-view-v1:fo-quoted-symbol (TPTP:LexicalValue ?value))
      (tptp-fo:symbol-name (tptp-fo:symbol-quoted) ?value))) (body))
    (rule fo-symbol-defined (head (metta-equation
      (tptp-view-v1:fo-symbol (tptp-rec:word-defined ?raw))
      (tptp-fo:symbol-name (tptp-fo:symbol-defined) ?raw))) (body))
    (rule fo-symbol-system (head (metta-equation
      (tptp-view-v1:fo-symbol (tptp-rec:word-system ?raw))
      (tptp-fo:symbol-name (tptp-fo:symbol-system) ?raw))) (body))
    (rule cnf-symbol-lower (head (metta-equation
      (tptp-view-v1:cnf-symbol (tptp-rec:word-lower ?raw))
      (tptp-view-v1:cnf-lower-symbol
        (tptp-lex-v1:word-value (tptp-rec:word-lower ?raw))))) (body))
    (rule cnf-lower-symbol (head (metta-equation
      (tptp-view-v1:cnf-lower-symbol (TPTP:LexicalValue ?value))
      (fo-cnf:symbol-name (fo-cnf:symbol-lower) ?value))) (body))
    (rule cnf-symbol-quoted (head (metta-equation
      (tptp-view-v1:cnf-symbol (tptp-rec:word-quoted ?raw))
      (tptp-view-v1:cnf-quoted-symbol
        (tptp-lex-v1:word-value (tptp-rec:word-quoted ?raw))))) (body))
    (rule cnf-quoted-symbol (head (metta-equation
      (tptp-view-v1:cnf-quoted-symbol (TPTP:LexicalValue ?value))
      (fo-cnf:symbol-name (fo-cnf:symbol-quoted) ?value))) (body))
    (rule cnf-symbol-defined (head (metta-equation
      (tptp-view-v1:cnf-symbol (tptp-rec:word-defined ?raw))
      (fo-cnf:symbol-name (fo-cnf:symbol-defined) ?raw))) (body))
    (rule cnf-symbol-system (head (metta-equation
      (tptp-view-v1:cnf-symbol (tptp-rec:word-system ?raw))
      (fo-cnf:symbol-name (fo-cnf:symbol-system) ?raw))) (body))

    (rule fo-term-variable (head (metta-equation
      (tptp-view-v1:fo-term
        (tptp-rec:var (tptp-rec:word-upper ?name)) ?scope)
      (tptp-fo:term-variable
        (tptp-fo:variable-id ?scope (tptp-fo:variable-name ?name))))) (body))
    (rule fo-term-function (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:fun ?head ?arguments) ?scope)
      (tptp-fo:term-function (tptp-view-v1:fo-symbol ?head)
        (tptp-view-v1:fo-terms ?arguments ?scope)))) (body))
    (rule fo-term-lower (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:word-lower ?raw) ?scope)
      (tptp-fo:term-function (tptp-view-v1:fo-symbol (tptp-rec:word-lower ?raw))
        (tptp-fo:terms-nil)))) (body))
    (rule fo-term-quoted (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:word-quoted ?raw) ?scope)
      (tptp-fo:term-function (tptp-view-v1:fo-symbol (tptp-rec:word-quoted ?raw))
        (tptp-fo:terms-nil)))) (body))
    (rule fo-term-defined (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:word-defined ?raw) ?scope)
      (tptp-fo:term-function (tptp-view-v1:fo-symbol (tptp-rec:word-defined ?raw))
        (tptp-fo:terms-nil)))) (body))
    (rule fo-term-system (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:word-system ?raw) ?scope)
      (tptp-fo:term-function (tptp-view-v1:fo-symbol (tptp-rec:word-system ?raw))
        (tptp-fo:terms-nil)))) (body))
    (rule fo-term-integer (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:number-integer ?raw) ?scope)
      (tptp-fo:term-number (tptp-fo:number-integer) ?raw))) (body))
    (rule fo-term-rational (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:number-rational ?raw) ?scope)
      (tptp-fo:term-number (tptp-fo:number-rational) ?raw))) (body))
    (rule fo-term-real (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:number-real ?raw) ?scope)
      (tptp-fo:term-number (tptp-fo:number-real) ?raw))) (body))
    (rule fo-term-distinct (head (metta-equation
      (tptp-view-v1:fo-term (tptp-rec:distinct-object ?raw) ?scope)
      (tptp-fo:term-distinct ?raw))) (body))
    (rule fo-terms-nil (head (metta-equation
      (tptp-view-v1:fo-terms (tptp-rec:terms-nil) ?scope)
      (tptp-fo:terms-nil))) (body))
    (rule fo-terms-cons (head (metta-equation
      (tptp-view-v1:fo-terms (tptp-rec:terms-cons ?head ?tail) ?scope)
      (tptp-fo:terms-cons (tptp-view-v1:fo-term ?head ?scope)
        (tptp-view-v1:fo-terms ?tail ?scope)))) (body))

    (rule cnf-term-variable (head (metta-equation
      (tptp-view-v1:cnf-term
        (tptp-rec:var (tptp-rec:word-upper ?name)) ?scope)
      (fo-cnf:term-variable
        (fo-cnf:variable-id ?scope (fo-cnf:variable-name ?name))))) (body))
    (rule cnf-term-function (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:fun ?head ?arguments) ?scope)
      (fo-cnf:term-function (tptp-view-v1:cnf-symbol ?head)
        (tptp-view-v1:cnf-terms ?arguments ?scope)))) (body))
    (rule cnf-term-lower (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:word-lower ?raw) ?scope)
      (fo-cnf:term-function (tptp-view-v1:cnf-symbol (tptp-rec:word-lower ?raw))
        (fo-cnf:terms-nil)))) (body))
    (rule cnf-term-quoted (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:word-quoted ?raw) ?scope)
      (fo-cnf:term-function (tptp-view-v1:cnf-symbol (tptp-rec:word-quoted ?raw))
        (fo-cnf:terms-nil)))) (body))
    (rule cnf-term-defined (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:word-defined ?raw) ?scope)
      (fo-cnf:term-function (tptp-view-v1:cnf-symbol (tptp-rec:word-defined ?raw))
        (fo-cnf:terms-nil)))) (body))
    (rule cnf-term-system (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:word-system ?raw) ?scope)
      (fo-cnf:term-function (tptp-view-v1:cnf-symbol (tptp-rec:word-system ?raw))
        (fo-cnf:terms-nil)))) (body))
    (rule cnf-term-integer (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:number-integer ?raw) ?scope)
      (fo-cnf:term-number (fo-cnf:number-integer) ?raw))) (body))
    (rule cnf-term-rational (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:number-rational ?raw) ?scope)
      (fo-cnf:term-number (fo-cnf:number-rational) ?raw))) (body))
    (rule cnf-term-real (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:number-real ?raw) ?scope)
      (fo-cnf:term-number (fo-cnf:number-real) ?raw))) (body))
    (rule cnf-term-distinct (head (metta-equation
      (tptp-view-v1:cnf-term (tptp-rec:distinct-object ?raw) ?scope)
      (fo-cnf:term-distinct ?raw))) (body))
    (rule cnf-terms-nil (head (metta-equation
      (tptp-view-v1:cnf-terms (tptp-rec:terms-nil) ?scope)
      (fo-cnf:terms-nil))) (body))
    (rule cnf-terms-cons (head (metta-equation
      (tptp-view-v1:cnf-terms (tptp-rec:terms-cons ?head ?tail) ?scope)
      (fo-cnf:terms-cons (tptp-view-v1:cnf-term ?head ?scope)
        (tptp-view-v1:cnf-terms ?tail ?scope)))) (body))

    (rule fo-atom (head (metta-equation
      (tptp-view-v1:fo-atom ?predicate ?arguments ?scope)
      (tptp-fo:atom-predicate (tptp-view-v1:fo-symbol ?predicate)
        (tptp-view-v1:fo-terms ?arguments ?scope)))) (body))
    (rule cnf-atom (head (metta-equation
      (tptp-view-v1:cnf-atom ?predicate ?arguments ?scope)
      (fo-cnf:atom-predicate (tptp-view-v1:cnf-symbol ?predicate)
        (tptp-view-v1:cnf-terms ?arguments ?scope)))) (body))

    (rule fo-formula-atom (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:atom ?head ?arguments) ?scope)
      (tptp-fo:formula-atom (tptp-view-v1:fo-atom ?head ?arguments ?scope)))) (body))
    (rule fo-formula-equals (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:equals ?left ?right) ?scope)
      (tptp-fo:formula-atom (tptp-fo:atom-equality
        (tptp-view-v1:fo-term ?left ?scope)
        (tptp-view-v1:fo-term ?right ?scope))))) (body))
    (rule fo-formula-not-equals (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:not-equals ?left ?right) ?scope)
      (tptp-fo:formula-not (tptp-fo:formula-atom (tptp-fo:atom-equality
        (tptp-view-v1:fo-term ?left ?scope)
        (tptp-view-v1:fo-term ?right ?scope)))))) (body))
    (rule fo-formula-not (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:not ?body) ?scope)
      (tptp-fo:formula-not (tptp-view-v1:fo-formula ?body ?scope)))) (body))
    (rule fo-formula-and (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:and ?left ?right) ?scope)
      (tptp-fo:formula-and (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-or (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:or ?left ?right) ?scope)
      (tptp-fo:formula-or (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-iff (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:iff ?left ?right) ?scope)
      (tptp-fo:formula-iff (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-implies (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:implies ?left ?right) ?scope)
      (tptp-fo:formula-implies (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-reverse-implies (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:reverse-implies ?left ?right) ?scope)
      (tptp-fo:formula-reverse-implies
        (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-xor (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:xor ?left ?right) ?scope)
      (tptp-fo:formula-xor (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-nor (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:nor ?left ?right) ?scope)
      (tptp-fo:formula-nor (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-nand (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:nand ?left ?right) ?scope)
      (tptp-fo:formula-nand (tptp-view-v1:fo-formula ?left ?scope)
        (tptp-view-v1:fo-formula ?right ?scope)))) (body))
    (rule fo-formula-forall (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:forall ?variables ?body) ?scope)
      (tptp-fo:formula-forall (tptp-view-v1:fo-vars ?variables ?scope)
        (tptp-view-v1:fo-formula ?body ?scope)))) (body))
    (rule fo-formula-exists (head (metta-equation
      (tptp-view-v1:fo-formula (tptp-rec:exists ?variables ?body) ?scope)
      (tptp-fo:formula-exists (tptp-view-v1:fo-vars ?variables ?scope)
        (tptp-view-v1:fo-formula ?body ?scope)))) (body))
    (rule fo-vars-nil (head (metta-equation
      (tptp-view-v1:fo-vars (tptp-rec:vars-nil) ?scope)
      (tptp-fo:variables-nil))) (body))
    (rule fo-vars-cons (head (metta-equation
      (tptp-view-v1:fo-vars
        (tptp-rec:vars-cons (tptp-rec:var (tptp-rec:word-upper ?name)) ?tail)
        ?scope)
      (tptp-fo:variables-cons
        (tptp-fo:variable-id ?scope (tptp-fo:variable-name ?name))
        (tptp-view-v1:fo-vars ?tail ?scope)))) (body))
    (rule fo-vars-typed (head (metta-equation
      (tptp-view-v1:fo-vars
        (tptp-rec:vars-cons (tptp-rec:typed-var ?variable ?type) ?tail) ?scope)
      (tptp-view-v1:fo-vars (tptp-rec:vars-cons ?variable ?tail) ?scope))) (body))

    (rule cnf-literals-or (head (metta-equation
      (tptp-view-v1:cnf-literals (tptp-rec:or ?left ?right) ?scope)
      (tptp-view-v1:cnf-literals-append
        (tptp-view-v1:cnf-literals ?left ?scope)
        (tptp-view-v1:cnf-literals ?right ?scope)))) (body))
    (rule cnf-literals-positive (head (metta-equation
      (tptp-view-v1:cnf-literals (tptp-rec:atom ?head ?arguments) ?scope)
      (fo-cnf:literals-cons
        (fo-cnf:literal-positive (tptp-view-v1:cnf-atom ?head ?arguments ?scope))
        (fo-cnf:literals-nil)))) (body))
    (rule cnf-literals-equals (head (metta-equation
      (tptp-view-v1:cnf-literals (tptp-rec:equals ?left ?right) ?scope)
      (fo-cnf:literals-cons (fo-cnf:literal-positive (fo-cnf:atom-equality
        (tptp-view-v1:cnf-term ?left ?scope)
        (tptp-view-v1:cnf-term ?right ?scope))) (fo-cnf:literals-nil)))) (body))
    (rule cnf-literals-not-equals (head (metta-equation
      (tptp-view-v1:cnf-literals (tptp-rec:not-equals ?left ?right) ?scope)
      (fo-cnf:literals-cons (fo-cnf:literal-negative (fo-cnf:atom-equality
        (tptp-view-v1:cnf-term ?left ?scope)
        (tptp-view-v1:cnf-term ?right ?scope))) (fo-cnf:literals-nil)))) (body))
    (rule cnf-literals-negative-atom (head (metta-equation
      (tptp-view-v1:cnf-literals
        (tptp-rec:not (tptp-rec:atom ?head ?arguments)) ?scope)
      (fo-cnf:literals-cons
        (fo-cnf:literal-negative (tptp-view-v1:cnf-atom ?head ?arguments ?scope))
        (fo-cnf:literals-nil)))) (body))
    (rule cnf-literals-negative-equality (head (metta-equation
      (tptp-view-v1:cnf-literals
        (tptp-rec:not (tptp-rec:equals ?left ?right)) ?scope)
      (fo-cnf:literals-cons (fo-cnf:literal-negative (fo-cnf:atom-equality
        (tptp-view-v1:cnf-term ?left ?scope)
        (tptp-view-v1:cnf-term ?right ?scope))) (fo-cnf:literals-nil)))) (body))
    (rule cnf-literals-append-nil (head (metta-equation
      (tptp-view-v1:cnf-literals-append (fo-cnf:literals-nil) ?right)
      ?right)) (body))
    (rule cnf-literals-append-cons (head (metta-equation
      (tptp-view-v1:cnf-literals-append
        (fo-cnf:literals-cons ?head ?tail) ?right)
      (fo-cnf:literals-cons ?head
        (tptp-view-v1:cnf-literals-append ?tail ?right)))) (body))

    (rule fo-span (head (metta-equation
      (tptp-view-v1:fo-span (tptp-rec:span ?start ?stop))
      (tptp-fo:source-span ?start ?stop))) (body))
    (rule cnf-span (head (metta-equation
      (tptp-view-v1:cnf-span (tptp-rec:span ?start ?stop))
      (fo-cnf:source-span ?start ?stop))) (body))
    (rule fo-annotation-none (head (metta-equation
      (tptp-view-v1:fo-annotation (tptp-rec:annotation-none))
      (tptp-fo:annotation-none))) (body))
    (rule fo-annotation-tree (head (metta-equation
      (tptp-view-v1:fo-annotation (tptp-rec:annotation ?source ?optional))
      (tptp-fo:annotation-tree (tptp-view-v1:fo-syntax
        (tptp-view-v1:syntax (tptp-rec:annotation ?source ?optional)))))) (body))
    (rule cnf-annotation-none (head (metta-equation
      (tptp-view-v1:cnf-annotation (tptp-rec:annotation-none))
      (fo-cnf:annotation-none))) (body))
    (rule cnf-annotation-tree (head (metta-equation
      (tptp-view-v1:cnf-annotation (tptp-rec:annotation ?source ?optional))
      (fo-cnf:annotation-tree (tptp-view-v1:cnf-syntax
        (tptp-view-v1:syntax (tptp-rec:annotation ?source ?optional)))))) (body))

    (rule tag0 (head (metta-equation
      (tptp-view-v1:tag0 ?tag)
      (tptp-view-v1:syntax-list (tptp-view-v1:syntax-nodes-cons
        (tptp-view-v1:syntax-atom ?tag) (tptp-view-v1:syntax-nodes-nil))))) (body))
    (rule tag1 (head (metta-equation
      (tptp-view-v1:tag1 ?tag ?a)
      (tptp-view-v1:syntax-list (tptp-view-v1:syntax-nodes-cons
        (tptp-view-v1:syntax-atom ?tag) (tptp-view-v1:syntax-nodes-cons
          ?a (tptp-view-v1:syntax-nodes-nil)))))) (body))
    (rule tag2 (head (metta-equation
      (tptp-view-v1:tag2 ?tag ?a ?b)
      (tptp-view-v1:syntax-list (tptp-view-v1:syntax-nodes-cons
        (tptp-view-v1:syntax-atom ?tag) (tptp-view-v1:syntax-nodes-cons ?a
          (tptp-view-v1:syntax-nodes-cons ?b
            (tptp-view-v1:syntax-nodes-nil))))))) (body))
    (rule tag3 (head (metta-equation
      (tptp-view-v1:tag3 ?tag ?a ?b ?c)
      (tptp-view-v1:syntax-list (tptp-view-v1:syntax-nodes-cons
        (tptp-view-v1:syntax-atom ?tag) (tptp-view-v1:syntax-nodes-cons ?a
          (tptp-view-v1:syntax-nodes-cons ?b
            (tptp-view-v1:syntax-nodes-cons ?c
              (tptp-view-v1:syntax-nodes-nil)))))))) (body))

    ; Annotation syntax is an exact structural encoding of the compact tree.
    ; Tags preserve the source constructor; string leaves preserve the lexeme.
    (rule syntax-word-lower (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:word-lower ?raw))
      (tptp-view-v1:tag1 "word-lower" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-word-quoted (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:word-quoted ?raw))
      (tptp-view-v1:tag1 "word-quoted" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-word-back (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:word-back ?raw))
      (tptp-view-v1:tag1 "word-back" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-word-defined (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:word-defined ?raw))
      (tptp-view-v1:tag1 "word-defined" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-word-system (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:word-system ?raw))
      (tptp-view-v1:tag1 "word-system" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-word-upper (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:word-upper ?raw))
      (tptp-view-v1:tag1 "word-upper" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-number-integer (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:number-integer ?raw))
      (tptp-view-v1:tag1 "number-integer" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-number-rational (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:number-rational ?raw))
      (tptp-view-v1:tag1 "number-rational" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-number-real (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:number-real ?raw))
      (tptp-view-v1:tag1 "number-real" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-distinct (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:distinct-object ?raw))
      (tptp-view-v1:tag1 "distinct-object" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-name-word (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:name-word ?word))
      (tptp-view-v1:tag1 "name-word" (tptp-view-v1:syntax ?word)))) (body))
    (rule syntax-name-integer (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:name-integer ?raw))
      (tptp-view-v1:tag1 "name-integer" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-name-none (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:name-none))
      (tptp-view-v1:tag0 "name-none"))) (body))
    (rule syntax-role (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:role ?raw))
      (tptp-view-v1:tag1 "role" (tptp-view-v1:syntax-string ?raw)))) (body))
    (rule syntax-annotation-none (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:annotation-none))
      (tptp-view-v1:tag0 "annotation-none"))) (body))
    (rule syntax-annotation (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:annotation ?source ?optional))
      (tptp-view-v1:tag2 "annotation" (tptp-view-v1:syntax ?source)
        (tptp-view-v1:syntax ?optional)))) (body))
    (rule syntax-optional-none (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:optional-none))
      (tptp-view-v1:tag0 "optional-none"))) (body))
    (rule syntax-optional-info (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:optional-info ?info))
      (tptp-view-v1:tag1 "optional-info" (tptp-view-v1:syntax ?info)))) (body))
    (rule syntax-atom (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:atom ?head ?arguments))
      (tptp-view-v1:tag2 "atom" (tptp-view-v1:syntax ?head)
        (tptp-view-v1:syntax ?arguments)))) (body))
    (rule syntax-equals (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:equals ?left ?right))
      (tptp-view-v1:tag2 "equals" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-not-equals (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:not-equals ?left ?right))
      (tptp-view-v1:tag2 "not-equals" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-not (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:not ?body))
      (tptp-view-v1:tag1 "not" (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-and (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:and ?left ?right))
      (tptp-view-v1:tag2 "and" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-or (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:or ?left ?right))
      (tptp-view-v1:tag2 "or" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-iff (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:iff ?left ?right))
      (tptp-view-v1:tag2 "iff" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-implies (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:implies ?left ?right))
      (tptp-view-v1:tag2 "implies" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-reverse-implies (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:reverse-implies ?left ?right))
      (tptp-view-v1:tag2 "reverse-implies" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-xor (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:xor ?left ?right))
      (tptp-view-v1:tag2 "xor" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-nor (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:nor ?left ?right))
      (tptp-view-v1:tag2 "nor" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-nand (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:nand ?left ?right))
      (tptp-view-v1:tag2 "nand" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-forall (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:forall ?variables ?body))
      (tptp-view-v1:tag2 "forall" (tptp-view-v1:syntax ?variables)
        (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-exists (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:exists ?variables ?body))
      (tptp-view-v1:tag2 "exists" (tptp-view-v1:syntax ?variables)
        (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-apply (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:apply ?left ?right))
      (tptp-view-v1:tag2 "apply" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-typing (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:typing ?left ?right))
      (tptp-view-v1:tag2 "typing" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-map-type (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:map-type ?left ?right))
      (tptp-view-v1:tag2 "map-type" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-xprod (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:xprod ?left ?right))
      (tptp-view-v1:tag2 "xprod" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-fun (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:fun ?head ?arguments))
      (tptp-view-v1:tag2 "fun" (tptp-view-v1:syntax ?head)
        (tptp-view-v1:syntax ?arguments)))) (body))
    (rule syntax-var (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:var ?name))
      (tptp-view-v1:tag1 "var" (tptp-view-v1:syntax ?name)))) (body))
    (rule syntax-terms-nil (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:terms-nil))
      (tptp-view-v1:tag0 "terms-nil"))) (body))
    (rule syntax-terms-cons (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:terms-cons ?head ?tail))
      (tptp-view-v1:tag2 "terms-cons" (tptp-view-v1:syntax ?head)
        (tptp-view-v1:syntax ?tail)))) (body))
    (rule syntax-vars-nil (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:vars-nil))
      (tptp-view-v1:tag0 "vars-nil"))) (body))
    (rule syntax-vars-cons (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:vars-cons ?head ?tail))
      (tptp-view-v1:tag2 "vars-cons" (tptp-view-v1:syntax ?head)
        (tptp-view-v1:syntax ?tail)))) (body))
    (rule syntax-typed-var (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:typed-var ?variable ?type))
      (tptp-view-v1:tag2 "typed-var" (tptp-view-v1:syntax ?variable)
        (tptp-view-v1:syntax ?type)))) (body))
    (rule syntax-include-all (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:include-all))
      (tptp-view-v1:tag0 "include-all"))) (body))
    (rule syntax-include-names (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:include-names ?names))
      (tptp-view-v1:tag1 "include-names" (tptp-view-v1:syntax ?names)))) (body))
    (rule syntax-include-star (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:include-star))
      (tptp-view-v1:tag0 "include-star"))) (body))
    (rule syntax-names-nil (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:names-nil))
      (tptp-view-v1:tag0 "names-nil"))) (body))
    (rule syntax-names-cons (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:names-cons ?head ?tail))
      (tptp-view-v1:tag2 "names-cons" (tptp-view-v1:syntax ?head)
        (tptp-view-v1:syntax ?tail)))) (body))
    (rule syntax-inference (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:inference ?rule ?info ?parents))
      (tptp-view-v1:tag3 "inference" (tptp-view-v1:syntax ?rule)
        (tptp-view-v1:syntax ?info) (tptp-view-v1:syntax ?parents)))) (body))
    (rule syntax-unknown (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:unknown))
      (tptp-view-v1:tag0 "unknown"))) (body))
    (rule syntax-source-unknown (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:source-unknown))
      (tptp-view-v1:tag0 "source-unknown"))) (body))
    (rule syntax-file-source (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:file-source ?file ?name))
      (tptp-view-v1:tag2 "file-source" (tptp-view-v1:syntax ?file)
        (tptp-view-v1:syntax ?name)))) (body))
    (rule syntax-introduced (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:introduced ?type ?info ?parents))
      (tptp-view-v1:tag3 "introduced" (tptp-view-v1:syntax ?type)
        (tptp-view-v1:syntax ?info) (tptp-view-v1:syntax ?parents)))) (body))
    (rule syntax-creator (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:creator ?name ?info ?parents))
      (tptp-view-v1:tag3 "creator" (tptp-view-v1:syntax ?name)
        (tptp-view-v1:syntax ?info) (tptp-view-v1:syntax ?parents)))) (body))
    (rule syntax-theory (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:theory ?name ?info))
      (tptp-view-v1:tag2 "theory" (tptp-view-v1:syntax ?name)
        (tptp-view-v1:syntax ?info)))) (body))
    (rule syntax-let (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:let ?types ?definitions ?body))
      (tptp-view-v1:tag3 "let" (tptp-view-v1:syntax ?types)
        (tptp-view-v1:syntax ?definitions) (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-long-connective (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:long-connective ?name ?parameters))
      (tptp-view-v1:tag2 "long-connective" (tptp-view-v1:syntax ?name)
        (tptp-view-v1:syntax ?parameters)))) (body))
    (rule syntax-prefix (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:prefix ?operator ?body))
      (tptp-view-v1:tag2 "prefix" (tptp-view-v1:syntax ?operator)
        (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-gen-fun (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:gen-fun ?head ?arguments))
      (tptp-view-v1:tag2 "gen-fun" (tptp-view-v1:syntax ?head)
        (tptp-view-v1:syntax ?arguments)))) (body))
    (rule syntax-gen-list (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:gen-list ?items))
      (tptp-view-v1:tag1 "gen-list" (tptp-view-v1:syntax ?items)))) (body))
    (rule syntax-gen-colon (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:gen-colon ?left ?right))
      (tptp-view-v1:tag2 "gen-colon" (tptp-view-v1:syntax ?left)
        (tptp-view-v1:syntax ?right)))) (body))
    (rule syntax-type-defined (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:type-defined ?name))
      (tptp-view-v1:tag1 "type-defined" (tptp-view-v1:syntax ?name)))) (body))
    (rule syntax-pi (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:pi ?variables ?body))
      (tptp-view-v1:tag2 "pi" (tptp-view-v1:syntax ?variables)
        (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-sigma (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:sigma ?variables ?body))
      (tptp-view-v1:tag2 "sigma" (tptp-view-v1:syntax ?variables)
        (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-lambda (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:lambda ?variables ?body))
      (tptp-view-v1:tag2 "lambda" (tptp-view-v1:syntax ?variables)
        (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-choice (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:choice ?variables ?body))
      (tptp-view-v1:tag2 "choice" (tptp-view-v1:syntax ?variables)
        (tptp-view-v1:syntax ?body)))) (body))
    (rule syntax-definite (head (metta-equation
      (tptp-view-v1:syntax (tptp-rec:definite ?variables ?body))
      (tptp-view-v1:tag2 "definite" (tptp-view-v1:syntax ?variables)
        (tptp-view-v1:syntax ?body)))) (body))

    (rule fo-syntax-atom (head (metta-equation
      (tptp-view-v1:fo-syntax (tptp-view-v1:syntax-atom ?value))
      (tptp-fo:syntax-atom ?value))) (body))
    (rule fo-syntax-string (head (metta-equation
      (tptp-view-v1:fo-syntax (tptp-view-v1:syntax-string ?value))
      (tptp-fo:syntax-string ?value))) (body))
    (rule fo-syntax-list (head (metta-equation
      (tptp-view-v1:fo-syntax (tptp-view-v1:syntax-list ?items))
      (tptp-fo:syntax-list (tptp-view-v1:fo-syntax-nodes ?items)))) (body))
    (rule fo-syntax-nodes-nil (head (metta-equation
      (tptp-view-v1:fo-syntax-nodes (tptp-view-v1:syntax-nodes-nil))
      (tptp-fo:syntax-nodes-nil))) (body))
    (rule fo-syntax-nodes-cons (head (metta-equation
      (tptp-view-v1:fo-syntax-nodes
        (tptp-view-v1:syntax-nodes-cons ?head ?tail))
      (tptp-fo:syntax-nodes-cons (tptp-view-v1:fo-syntax ?head)
        (tptp-view-v1:fo-syntax-nodes ?tail)))) (body))
    (rule cnf-syntax-atom (head (metta-equation
      (tptp-view-v1:cnf-syntax (tptp-view-v1:syntax-atom ?value))
      (fo-cnf:syntax-atom ?value))) (body))
    (rule cnf-syntax-string (head (metta-equation
      (tptp-view-v1:cnf-syntax (tptp-view-v1:syntax-string ?value))
      (fo-cnf:syntax-string ?value))) (body))
    (rule cnf-syntax-list (head (metta-equation
      (tptp-view-v1:cnf-syntax (tptp-view-v1:syntax-list ?items))
      (fo-cnf:syntax-list (tptp-view-v1:cnf-syntax-nodes ?items)))) (body))
    (rule cnf-syntax-nodes-nil (head (metta-equation
      (tptp-view-v1:cnf-syntax-nodes (tptp-view-v1:syntax-nodes-nil))
      (fo-cnf:syntax-nodes-nil))) (body))
    (rule cnf-syntax-nodes-cons (head (metta-equation
      (tptp-view-v1:cnf-syntax-nodes
        (tptp-view-v1:syntax-nodes-cons ?head ?tail))
      (fo-cnf:syntax-nodes-cons (tptp-view-v1:cnf-syntax ?head)
        (tptp-view-v1:cnf-syntax-nodes ?tail)))) (body))

    (rule fo-selection-all (head (metta-equation
      (tptp-view-v1:fo-selection (tptp-rec:include-all))
      (tptp-fo:include-all))) (body))
    (rule fo-selection-star (head (metta-equation
      (tptp-view-v1:fo-selection (tptp-rec:include-star))
      (tptp-fo:include-all))) (body))
    (rule fo-selection-names (head (metta-equation
      (tptp-view-v1:fo-selection (tptp-rec:include-names ?names))
      (tptp-fo:include-names (tptp-view-v1:fo-names ?names)))) (body))
    (rule fo-names-nil (head (metta-equation
      (tptp-view-v1:fo-names (tptp-rec:names-nil))
      (tptp-fo:formula-names-nil))) (body))
    (rule fo-names-cons (head (metta-equation
      (tptp-view-v1:fo-names (tptp-rec:names-cons ?head ?tail))
      (tptp-fo:formula-names-cons (tptp-view-v1:fo-name ?head)
        (tptp-view-v1:fo-names ?tail)))) (body))
    (rule fo-qualifier-none (head (metta-equation
      (tptp-view-v1:fo-qualifier (tptp-rec:name-none))
      (tptp-fo:formula-name-none))) (body))
    (rule fo-qualifier-some-word (head (metta-equation
      (tptp-view-v1:fo-qualifier (tptp-rec:name-word ?word))
      (tptp-fo:formula-name-some
        (tptp-view-v1:fo-name (tptp-rec:name-word ?word))))) (body))
    (rule fo-qualifier-some-integer (head (metta-equation
      (tptp-view-v1:fo-qualifier (tptp-rec:name-integer ?raw))
      (tptp-fo:formula-name-some
        (tptp-view-v1:fo-name (tptp-rec:name-integer ?raw))))) (body))
    (rule fo-include-path (head (metta-equation
      (tptp-view-v1:fo-include-path (tptp-rec:word-quoted ?raw))
      (tptp-view-v1:fo-quoted-symbol
        (tptp-lex-v1:word-value (tptp-rec:word-quoted ?raw))))) (body))))
