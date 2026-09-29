; Executable interpretation of the regex rows of the official Extended BNF.
; Produces the existing EBNF document and BNF lexical environment, not a new
; parser representation. Syntax and semantic rows remain in the original
; document returned alongside the lexical grammar; this is not a problem reader.
; The caller supplies the regex-dot scalar matcher explicitly. A dot within a
; class is always a literal. Character complements use the BNF Unicode domain.
; Scalar extents are ordered sets at the existing lexical-profile boundary;
; their source item order and overlaps remain in the retained regex document.
; Generated class names contain '#', outside official source row names. The
; binary occurrence counter does not identify equal or overlapping source spans.
; Requires the common BNF denotation codecs and EBNF binary counter sources.
(gslt-presentation-v1 TptpOfficialLexicalToEbnfV1
  (signature
    (operator metta-equation 2)
    (operator let 3)
    (operator + 2)
    (operator < 2)
    (operator langdef:ground-equal 2)
    (operator LNil 0) (operator LCons 2)
    (operator bnf-v1:text->string 1)
    (operator ebnf-v1:next-bits 1)
    (operator tptp-lex-v1:lower 3)
    (operator tptp-lex-v1:finish 2)
    (operator tptp-lex-v1:lowered 5)
    (operator tptp-lex-v1:state 6)
    (operator tptp-lex-v1:result 2)
    (operator tptp-lex-v1:text 1)
    (operator tptp-lex-v1:span 1)
    (operator tptp-lex-v1:entries 2)
    (operator tptp-lex-v1:prepend-rule 4)
    (operator tptp-lex-v1:join-rule 2)
    (operator tptp-lex-v1:finish-expression 2)
    (operator tptp-lex-v1:next-alternative 2)
    (operator tptp-lex-v1:join-alternative 2)
    (operator tptp-lex-v1:finish-alternative 2)
    (operator tptp-lex-v1:next-element 2)
    (operator tptp-lex-v1:join-element 2)
    (operator tptp-lex-v1:finish-group 2)
    (operator tptp-lex-v1:expression 2)
    (operator tptp-lex-v1:alternatives 2)
    (operator tptp-lex-v1:alternative 2)
    (operator tptp-lex-v1:elements 2)
    (operator tptp-lex-v1:element 2)
    (operator tptp-lex-v1:atom 2)
    (operator tptp-lex-v1:repeat 3)
    (operator tptp-lex-v1:class 3)
    (operator tptp-lex-v1:matcher 2)
    (operator tptp-lex-v1:items 2)
    (operator tptp-lex-v1:union 2)
    (operator tptp-lex-v1:union-left 3)
    (operator tptp-lex-v1:union-right 3)
    (operator tptp-lex-v1:item 2)
    (operator tptp-lex-v1:endpoint 1)
    (operator tptp-lex-v1:escape 1)
    (operator tptp-lex-v1:octal 2)
    (operator tptp-lex-v1:times-eight 1)
    (operator tptp-lex-v1:range 3)
    (operator tptp-lex-v1:range-order 4)
    (operator tptp-lex-v1:range-scalars 2)
    (operator langdef:codepoint-range 2)
    (operator tptp-lex-v1:invalid-range 2)
    (operator tptp-lex-v1:reverse-declarations 2)
    (operator tptp-lex-v1:reverse-origins 2)
    (operator tptp-lex-v1:origins-nil 0)
    (operator tptp-lex-v1:origins-cons 2)
    (operator tptp-lex-v1:class-origin 2)
    (operator tptp-ebnf-v1:text-nil 0)
    (operator tptp-ebnf-v1:text-cons 2)
    (operator tptp-ebnf-v1:source-span 2)
    (operator tptp-ebnf-v1:document 2)
    (operator tptp-ebnf-v1:entries-nil 0)
    (operator tptp-ebnf-v1:entries-cons 2)
    (operator tptp-ebnf-v1:syntax-row 3)
    (operator tptp-ebnf-v1:semantic-row 3)
    (operator tptp-ebnf-v1:token-row 3)
    (operator tptp-ebnf-v1:character-row 3)
    (operator tptp-ebnf-v1:comment 2)
    (operator tptp-ebnf-v1:blank 1)
    (operator tptp-ebnf-v1:regex-choice 2)
    (operator tptp-ebnf-v1:regex-sequences-nil 0)
    (operator tptp-ebnf-v1:regex-sequences-cons 2)
    (operator tptp-ebnf-v1:regex-sequence 2)
    (operator tptp-ebnf-v1:regex-elements-nil 0)
    (operator tptp-ebnf-v1:regex-elements-cons 2)
    (operator tptp-ebnf-v1:regex-piece 3)
    (operator tptp-ebnf-v1:regex-angle-reference 2)
    (operator tptp-ebnf-v1:regex-brace-reference 2)
    (operator tptp-ebnf-v1:regex-group 2)
    (operator tptp-ebnf-v1:regex-class 3)
    (operator tptp-ebnf-v1:regex-wildcard 1)
    (operator tptp-ebnf-v1:regex-once 0)
    (operator tptp-ebnf-v1:regex-zero-or-more 0)
    (operator tptp-ebnf-v1:regex-class-positive 0)
    (operator tptp-ebnf-v1:regex-class-negated 0)
    (operator tptp-ebnf-v1:regex-class-items-nil 0)
    (operator tptp-ebnf-v1:regex-class-items-cons 2)
    (operator tptp-ebnf-v1:regex-class-singleton 2)
    (operator tptp-ebnf-v1:regex-class-range 3)
    (operator tptp-ebnf-v1:regex-endpoint-scalar 2)
    (operator tptp-ebnf-v1:regex-endpoint-simple-escape 2)
    (operator tptp-ebnf-v1:regex-endpoint-octal-escape 2)
    (operator bnf-v1:text-nil 0)
    (operator bnf-v1:text-cons 2)
    (operator bnf-v1:source-span 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:entries-nil 0)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternatives-nil 0)
    (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:elements-nil 0)
    (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:reference 2)
    (operator ebnf-v1:group 2)
    (operator ebnf-v1:zero-or-more 2)
    (operator bnf-v1:lexical-environment 1)
    (operator bnf-v1:lexical-declarations-nil 0)
    (operator bnf-v1:lexical-declarations-cons 2)
    (operator bnf-v1:lexical-declaration 5)
    (operator bnf-v1:lexical-origin 2)
    (operator bnf-v1:lexical-points 1)
    (operator bnf-v1:lexical-except 1)
    (operator bnf-v1:scalars-nil 0)
    (operator bnf-v1:scalars-cons 2))
  (equations)
  (rewrites
    (rule tptp-lex-lower
      (head (metta-equation
        (tptp-lex-v1:lower (tptp-ebnf-v1:document ?entries ?span) ?source ?dot)
        (tptp-lex-v1:finish
          (tptp-lex-v1:entries ?entries
            (tptp-lex-v1:state (bnf-v1:text-nil) 0
              (bnf-v1:lexical-declarations-nil) (tptp-lex-v1:origins-nil) ?dot ?source))
          (tptp-ebnf-v1:document ?entries ?span)))) (body))
    (rule tptp-lex-finish
      (head (metta-equation
        (tptp-lex-v1:finish (tptp-lex-v1:result ?entries
          (tptp-lex-v1:state ?bits ?ordinal ?declarations ?origins ?dot ?source))
          (tptp-ebnf-v1:document ?original ?span))
        (tptp-lex-v1:lowered
          (bnf-v1:document ?entries (tptp-lex-v1:span ?span))
          (bnf-v1:lexical-environment (tptp-lex-v1:reverse-declarations ?declarations (bnf-v1:lexical-declarations-nil)))
          (tptp-lex-v1:reverse-origins ?origins (tptp-lex-v1:origins-nil))
          (tptp-ebnf-v1:document ?original ?span) ?dot))) (body))
    (rule tptp-lex-text-nil (head (metta-equation
      (tptp-lex-v1:text (tptp-ebnf-v1:text-nil)) (bnf-v1:text-nil))) (body))
    (rule tptp-lex-text-cons (head (metta-equation
      (tptp-lex-v1:text (tptp-ebnf-v1:text-cons ?cp ?tail))
      (bnf-v1:text-cons ?cp (tptp-lex-v1:text ?tail)))) (body))
    (rule tptp-lex-span (head (metta-equation
      (tptp-lex-v1:span (tptp-ebnf-v1:source-span ?a ?b))
      (bnf-v1:source-span ?a ?b))) (body))
    (rule tptp-lex-entries-empty (head (metta-equation
      (tptp-lex-v1:entries (tptp-ebnf-v1:entries-nil) ?state)
      (tptp-lex-v1:result (bnf-v1:entries-nil) ?state))) (body))
    (rule tptp-lex-entries-token (head (metta-equation
      (tptp-lex-v1:entries (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:token-row ?name ?rhs ?span) ?tail) ?state)
      (tptp-lex-v1:prepend-rule ?name ?span (tptp-lex-v1:expression ?rhs ?state) ?tail))) (body))
    (rule tptp-lex-entries-character (head (metta-equation
      (tptp-lex-v1:entries (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:character-row ?name ?rhs ?span) ?tail) ?state)
      (tptp-lex-v1:prepend-rule ?name ?span (tptp-lex-v1:expression ?rhs ?state) ?tail))) (body))
    (rule tptp-lex-entries-syntax (head (metta-equation
      (tptp-lex-v1:entries (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:syntax-row ?name ?rhs ?span) ?tail) ?state)
      (tptp-lex-v1:entries ?tail ?state))) (body))
    (rule tptp-lex-entries-semantic (head (metta-equation
      (tptp-lex-v1:entries (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:semantic-row ?name ?rhs ?span) ?tail) ?state)
      (tptp-lex-v1:entries ?tail ?state))) (body))
    (rule tptp-lex-entries-comment (head (metta-equation
      (tptp-lex-v1:entries (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:comment ?text ?span) ?tail) ?state)
      (tptp-lex-v1:entries ?tail ?state))) (body))
    (rule tptp-lex-entries-blank (head (metta-equation
      (tptp-lex-v1:entries (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:blank ?span) ?tail) ?state)
      (tptp-lex-v1:entries ?tail ?state))) (body))
    (rule tptp-lex-prepend-rule (head (metta-equation
      (tptp-lex-v1:prepend-rule ?name ?span (tptp-lex-v1:result ?expression ?state) ?tail)
      (tptp-lex-v1:join-rule
        (bnf-v1:rule (tptp-lex-v1:text ?name) ?expression (tptp-lex-v1:span ?span))
        (tptp-lex-v1:entries ?tail ?state)))) (body))
    (rule tptp-lex-join-rule (head (metta-equation
      (tptp-lex-v1:join-rule ?rule (tptp-lex-v1:result ?entries ?state))
      (tptp-lex-v1:result (bnf-v1:entries-cons ?rule ?entries) ?state))) (body))
    (rule tptp-lex-expression (head (metta-equation
      (tptp-lex-v1:expression (tptp-ebnf-v1:regex-choice ?alternatives ?span) ?state)
      (tptp-lex-v1:finish-expression (tptp-lex-v1:alternatives ?alternatives ?state) ?span))) (body))
    (rule tptp-lex-finish-expression (head (metta-equation
      (tptp-lex-v1:finish-expression (tptp-lex-v1:result ?values ?state) ?span)
      (tptp-lex-v1:result (bnf-v1:expression ?values (tptp-lex-v1:span ?span)) ?state))) (body))
    (rule tptp-lex-alternatives-empty (head (metta-equation
      (tptp-lex-v1:alternatives (tptp-ebnf-v1:regex-sequences-nil) ?state)
      (tptp-lex-v1:result (bnf-v1:alternatives-nil) ?state))) (body))
    (rule tptp-lex-alternatives-cons (head (metta-equation
      (tptp-lex-v1:alternatives (tptp-ebnf-v1:regex-sequences-cons ?head ?tail) ?state)
      (tptp-lex-v1:next-alternative (tptp-lex-v1:alternative ?head ?state) ?tail))) (body))
    (rule tptp-lex-next-alternative (head (metta-equation
      (tptp-lex-v1:next-alternative (tptp-lex-v1:result ?value ?state) ?tail)
      (tptp-lex-v1:join-alternative ?value (tptp-lex-v1:alternatives ?tail ?state)))) (body))
    (rule tptp-lex-join-alternative (head (metta-equation
      (tptp-lex-v1:join-alternative ?value (tptp-lex-v1:result ?rest ?state))
      (tptp-lex-v1:result (bnf-v1:alternatives-cons ?value ?rest) ?state))) (body))
    (rule tptp-lex-alternative (head (metta-equation
      (tptp-lex-v1:alternative (tptp-ebnf-v1:regex-sequence ?elements ?span) ?state)
      (tptp-lex-v1:finish-alternative (tptp-lex-v1:elements ?elements ?state) ?span))) (body))
    (rule tptp-lex-finish-alternative (head (metta-equation
      (tptp-lex-v1:finish-alternative (tptp-lex-v1:result ?values ?state) ?span)
      (tptp-lex-v1:result (bnf-v1:alternative ?values (tptp-lex-v1:span ?span)) ?state))) (body))
    (rule tptp-lex-elements-empty (head (metta-equation
      (tptp-lex-v1:elements (tptp-ebnf-v1:regex-elements-nil) ?state)
      (tptp-lex-v1:result (bnf-v1:elements-nil) ?state))) (body))
    (rule tptp-lex-elements-cons (head (metta-equation
      (tptp-lex-v1:elements (tptp-ebnf-v1:regex-elements-cons ?head ?tail) ?state)
      (tptp-lex-v1:next-element (tptp-lex-v1:element ?head ?state) ?tail))) (body))
    (rule tptp-lex-next-element (head (metta-equation
      (tptp-lex-v1:next-element (tptp-lex-v1:result ?value ?state) ?tail)
      (tptp-lex-v1:join-element ?value (tptp-lex-v1:elements ?tail ?state)))) (body))
    (rule tptp-lex-join-element (head (metta-equation
      (tptp-lex-v1:join-element ?value (tptp-lex-v1:result ?rest ?state))
      (tptp-lex-v1:result (bnf-v1:elements-cons ?value ?rest) ?state))) (body))
    (rule tptp-lex-element (head (metta-equation
      (tptp-lex-v1:element (tptp-ebnf-v1:regex-piece ?atom ?repetition ?span) ?state)
      (tptp-lex-v1:repeat ?repetition ?span (tptp-lex-v1:atom ?atom ?state)))) (body))
    (rule tptp-lex-once (head (metta-equation
      (tptp-lex-v1:repeat (tptp-ebnf-v1:regex-once) ?span ?result) ?result)) (body))
    (rule tptp-lex-star (head (metta-equation
      (tptp-lex-v1:repeat (tptp-ebnf-v1:regex-zero-or-more) ?span (tptp-lex-v1:result ?value ?state))
      (tptp-lex-v1:result (ebnf-v1:zero-or-more ?value (tptp-lex-v1:span ?span)) ?state))) (body))
    (rule tptp-lex-angle-reference (head (metta-equation
      (tptp-lex-v1:atom (tptp-ebnf-v1:regex-angle-reference ?name ?span) ?state)
      (tptp-lex-v1:result (bnf-v1:reference (tptp-lex-v1:text ?name) (tptp-lex-v1:span ?span)) ?state))) (body))
    (rule tptp-lex-brace-reference (head (metta-equation
      (tptp-lex-v1:atom (tptp-ebnf-v1:regex-brace-reference ?name ?span) ?state)
      (tptp-lex-v1:result (bnf-v1:reference (tptp-lex-v1:text ?name) (tptp-lex-v1:span ?span)) ?state))) (body))
    (rule tptp-lex-group (head (metta-equation
      (tptp-lex-v1:atom (tptp-ebnf-v1:regex-group ?expression ?span) ?state)
      (tptp-lex-v1:finish-group (tptp-lex-v1:expression ?expression ?state) ?span))) (body))
    (rule tptp-lex-finish-group (head (metta-equation
      (tptp-lex-v1:finish-group (tptp-lex-v1:result ?value ?state) ?span)
      (tptp-lex-v1:result (ebnf-v1:group ?value (tptp-lex-v1:span ?span)) ?state))) (body))
    (rule tptp-lex-character-class (head (metta-equation
      (tptp-lex-v1:atom (tptp-ebnf-v1:regex-class ?polarity ?items ?span) ?state)
      (tptp-lex-v1:class (tptp-lex-v1:matcher ?polarity (tptp-lex-v1:items ?items (bnf-v1:scalars-nil))) ?span ?state))) (body))
    (rule tptp-lex-wildcard (head (metta-equation
      (tptp-lex-v1:atom (tptp-ebnf-v1:regex-wildcard ?span)
        (tptp-lex-v1:state ?bits ?ordinal ?decls ?origins ?dot ?source))
      (tptp-lex-v1:class ?dot ?span (tptp-lex-v1:state ?bits ?ordinal ?decls ?origins ?dot ?source)))) (body))
    (rule tptp-lex-allocate-class (head (metta-equation
      (tptp-lex-v1:class ?matcher ?span
        (tptp-lex-v1:state ?bits ?ordinal ?decls ?origins ?dot ?source))
      (let ?name (bnf-v1:text-cons 35 ?bits)
        (let ?label (bnf-v1:text->string ?name)
          (tptp-lex-v1:result (bnf-v1:reference ?name (tptp-lex-v1:span ?span))
            (tptp-lex-v1:state (ebnf-v1:next-bits ?bits) (+ ?ordinal 1)
              (bnf-v1:lexical-declarations-cons
                (bnf-v1:lexical-declaration ?name ?label ?matcher ?label (bnf-v1:lexical-origin ?source ?ordinal)) ?decls)
              (tptp-lex-v1:origins-cons (tptp-lex-v1:class-origin ?name ?span) ?origins) ?dot ?source)))))) (body))
    (rule tptp-lex-positive (head (metta-equation
      (tptp-lex-v1:matcher (tptp-ebnf-v1:regex-class-positive) ?scalars)
      (bnf-v1:lexical-points ?scalars))) (body))
    (rule tptp-lex-negative (head (metta-equation
      (tptp-lex-v1:matcher (tptp-ebnf-v1:regex-class-negated) ?scalars)
      (bnf-v1:lexical-except ?scalars))) (body))
    (rule tptp-lex-items-empty (head (metta-equation
      (tptp-lex-v1:items (tptp-ebnf-v1:regex-class-items-nil) ?tail) ?tail)) (body))
    (rule tptp-lex-items-cons (head (metta-equation
      (tptp-lex-v1:items (tptp-ebnf-v1:regex-class-items-cons ?head ?tail) ?rest)
      (tptp-lex-v1:union (tptp-lex-v1:item ?head (bnf-v1:scalars-nil))
        (tptp-lex-v1:items ?tail ?rest)))) (body))
    (rule tptp-lex-union-empty (head (metta-equation
      (tptp-lex-v1:union (bnf-v1:scalars-nil) ?right) ?right)) (body))
    (rule tptp-lex-union-right-empty (head (metta-equation
      (tptp-lex-v1:union (bnf-v1:scalars-cons ?head ?tail) (bnf-v1:scalars-nil))
      (bnf-v1:scalars-cons ?head ?tail))) (body))
    (rule tptp-lex-union-cons (head (metta-equation
      (tptp-lex-v1:union (bnf-v1:scalars-cons ?a ?as) (bnf-v1:scalars-cons ?b ?bs))
      (tptp-lex-v1:union-left (< ?a ?b)
        (bnf-v1:scalars-cons ?a ?as) (bnf-v1:scalars-cons ?b ?bs)))) (body))
    (rule tptp-lex-union-first (head (metta-equation
      (tptp-lex-v1:union-left True (bnf-v1:scalars-cons ?a ?as) ?right)
      (bnf-v1:scalars-cons ?a (tptp-lex-v1:union ?as ?right)))) (body))
    (rule tptp-lex-union-not-first (head (metta-equation
      (tptp-lex-v1:union-left False (bnf-v1:scalars-cons ?a ?as) (bnf-v1:scalars-cons ?b ?bs))
      (tptp-lex-v1:union-right (< ?b ?a)
        (bnf-v1:scalars-cons ?a ?as) (bnf-v1:scalars-cons ?b ?bs)))) (body))
    (rule tptp-lex-union-second (head (metta-equation
      (tptp-lex-v1:union-right True ?left (bnf-v1:scalars-cons ?b ?bs))
      (bnf-v1:scalars-cons ?b (tptp-lex-v1:union ?left ?bs)))) (body))
    (rule tptp-lex-union-equal (head (metta-equation
      (tptp-lex-v1:union-right False (bnf-v1:scalars-cons ?a ?as) (bnf-v1:scalars-cons ?b ?bs))
      (bnf-v1:scalars-cons ?a (tptp-lex-v1:union ?as ?bs)))) (body))
    (rule tptp-lex-singleton (head (metta-equation
      (tptp-lex-v1:item (tptp-ebnf-v1:regex-class-singleton ?endpoint ?span) ?tail)
      (bnf-v1:scalars-cons (tptp-lex-v1:endpoint ?endpoint) ?tail))) (body))
    (rule tptp-lex-range (head (metta-equation
      (tptp-lex-v1:item (tptp-ebnf-v1:regex-class-range ?first ?last ?span) ?tail)
      (tptp-lex-v1:range (tptp-lex-v1:endpoint ?first) (tptp-lex-v1:endpoint ?last) ?tail))) (body))
    (rule tptp-lex-endpoint (head (metta-equation
      (tptp-lex-v1:endpoint (tptp-ebnf-v1:regex-endpoint-scalar
        (tptp-ebnf-v1:text-cons ?cp (tptp-ebnf-v1:text-nil)) ?span)) ?cp)) (body))
    (rule tptp-lex-endpoint-escape (head (metta-equation
      (tptp-lex-v1:endpoint (tptp-ebnf-v1:regex-endpoint-simple-escape
        (tptp-ebnf-v1:text-cons ?cp (tptp-ebnf-v1:text-nil)) ?span))
      (tptp-lex-v1:escape ?cp))) (body))
    (rule tptp-lex-endpoint-octal (head (metta-equation
      (tptp-lex-v1:endpoint (tptp-ebnf-v1:regex-endpoint-octal-escape ?digits ?span))
      (tptp-lex-v1:octal ?digits 0))) (body))
    (rule tptp-lex-escape-newline (head (metta-equation (tptp-lex-v1:escape 110) 10)) (body))
    (rule tptp-lex-escape-return (head (metta-equation (tptp-lex-v1:escape 114) 13)) (body))
    (rule tptp-lex-escape-tab (head (metta-equation (tptp-lex-v1:escape 116) 9)) (body))
    (rule tptp-lex-escape-backslash (head (metta-equation (tptp-lex-v1:escape 92) 92)) (body))
    (rule tptp-lex-escape-double-quote (head (metta-equation (tptp-lex-v1:escape 34) 34)) (body))
    (rule tptp-lex-escape-single-quote (head (metta-equation (tptp-lex-v1:escape 39) 39)) (body))
    (rule tptp-lex-octal-empty (head (metta-equation
      (tptp-lex-v1:octal (tptp-ebnf-v1:text-nil) ?value) ?value)) (body))
    (rule tptp-lex-octal-cons (head (metta-equation
      (tptp-lex-v1:octal (tptp-ebnf-v1:text-cons ?digit ?tail) ?value)
      (tptp-lex-v1:octal ?tail (+ (tptp-lex-v1:times-eight ?value) (+ ?digit -48))))) (body))
    (rule tptp-lex-times-eight (head (metta-equation
      (tptp-lex-v1:times-eight ?value)
      (let ?twice (+ ?value ?value) (let ?four (+ ?twice ?twice) (+ ?four ?four))))) (body))
    (rule tptp-lex-range-dispatch (head (metta-equation
      (tptp-lex-v1:range ?first ?last ?tail)
      (tptp-lex-v1:range-order (< ?last ?first) ?first ?last ?tail))) (body))
    (rule tptp-lex-range-reversed (head (metta-equation
      (tptp-lex-v1:range-order True ?first ?last ?tail)
      (tptp-lex-v1:invalid-range ?first ?last))) (body))
    (rule tptp-lex-range-ordered (head (metta-equation
      (tptp-lex-v1:range-order False ?first ?last ?tail)
      (tptp-lex-v1:range-scalars (langdef:codepoint-range ?first ?last) ?tail))) (body))
    (rule tptp-lex-range-scalars-end (head (metta-equation
      (tptp-lex-v1:range-scalars LNil ?tail) ?tail)) (body))
    (rule tptp-lex-range-scalars-more (head (metta-equation
      (tptp-lex-v1:range-scalars (LCons ?cp ?rest) ?tail)
      (bnf-v1:scalars-cons ?cp (tptp-lex-v1:range-scalars ?rest ?tail)))) (body))
    (rule tptp-lex-reverse-declarations-empty (head (metta-equation
      (tptp-lex-v1:reverse-declarations (bnf-v1:lexical-declarations-nil) ?tail) ?tail)) (body))
    (rule tptp-lex-reverse-declarations-cons (head (metta-equation
      (tptp-lex-v1:reverse-declarations (bnf-v1:lexical-declarations-cons ?head ?rest) ?tail)
      (tptp-lex-v1:reverse-declarations ?rest (bnf-v1:lexical-declarations-cons ?head ?tail)))) (body))
    (rule tptp-lex-reverse-origins-empty (head (metta-equation
      (tptp-lex-v1:reverse-origins (tptp-lex-v1:origins-nil) ?tail) ?tail)) (body))
    (rule tptp-lex-reverse-origins-cons (head (metta-equation
      (tptp-lex-v1:reverse-origins (tptp-lex-v1:origins-cons ?head ?rest) ?tail)
      (tptp-lex-v1:reverse-origins ?rest (tptp-lex-v1:origins-cons ?head ?tail)))) (body))))
