; Source-grammar observation of the composed TPTP EBNF derivations.
; This is a reference projection, not the compact public formula view.
; Production names/alternatives come from the executed grammar. Lexical
; wrappers are classified from the lexical adapter's output, not a second
; list of TPTP tokens. Original source and the EBNF receipt remain with the
; caller; layout is absent from this observation, not erased from that source.
; Distinct root derivations remain distinct list occurrences.
; Positions use the EBNF engine's Unicode-scalar coordinates, not UTF-8 bytes.
; The production label extends the structural CST vocabulary; it is not a
; value admitted by the older, fixed FOF/CNF syntax-tree carrier.
(gslt-presentation-v1 TptpOfficialSyntaxObservationV1
  (signature
    (operator metta-equation 2) (operator let 3)
    (operator langdef:ground-equal 2)
    (operator bnf-v1:text->string 1) (operator bnf-v1:string->text 1)
    (operator bnf-v1:text-append 2) (operator tptp-syn-v1:token-name 1)
    (operator tptp-lex-v1:text 1)
    (operator tptp-lex-v1:lowered 5)
    (operator bnf-v1:document 2) (operator bnf-v1:entries-nil 0)
    (operator bnf-v1:entries-cons 2) (operator bnf-v1:rule 3)
    (operator bnf-v1:text-nil 0) (operator bnf-v1:text-cons 2)
    (operator LNil 0) (operator LCons 2)
    (operator EBNF:DerivationsNil 0) (operator EBNF:DerivationsCons 2)
    (operator EBNF:Rule 6) (operator EBNF:Reference 4)
    (operator EBNF:Group 6) (operator EBNF:Repetition 5)
    (operator EBNF:OptionalAbsent 3) (operator EBNF:OptionalPresent 4)
    (operator EBNF:ElementsNil 0) (operator EBNF:ElementsCons 2)
    (operator EBNF:IterationsNil 0) (operator EBNF:IterationsCons 2)
    (operator EBNF:Literal 3) (operator EBNF:Lexical 5)
    (operator EBNF:InputSpan 2)
    (operator tptp-cst:spanned-node 4) (operator tptp-cst:production 2)
    (operator tptp-cst:token 4) (operator tptp-cst:literal 3)
    (operator tptp-read-v1:prepare 1) (operator tptp-read-v1:lexicals 1)
    (operator tptp-read-v1:source-names 2)
    (operator tptp-read-v1:source-name 2)
    (operator tptp-ebnf-v1:document 2)
    (operator tptp-ebnf-v1:entries-nil 0) (operator tptp-ebnf-v1:entries-cons 2)
    (operator tptp-ebnf-v1:syntax-row 3) (operator tptp-ebnf-v1:semantic-row 3)
    (operator tptp-ebnf-v1:token-row 3) (operator tptp-ebnf-v1:character-row 3)
    (operator tptp-ebnf-v1:comment 2) (operator tptp-ebnf-v1:blank 1)
    (operator tptp-read-v1:roots 2) (operator tptp-read-v1:node 3)
    (operator tptp-read-v1:elements 3) (operator tptp-read-v1:iterations 3)
    (operator tptp-read-v1:kind 2) (operator tptp-read-v1:entry-kind 3)
    (operator tptp-read-v1:layout-kind 3) (operator tptp-read-v1:lookup 2)
    (operator tptp-read-v1:lookup-next 4)
    (operator tptp-read-v1:entry 0) (operator tptp-read-v1:layout 0)
    (operator tptp-read-v1:syntax 0) (operator tptp-read-v1:token 1)
    (operator tptp-read-v1:rule 7)
    (operator tptp-read-v1:checked-token 6)
    (operator tptp-read-v1:characters 2)
    (operator tptp-read-v1:character-elements 2)
    (operator tptp-read-v1:character-iterations 2))
  (equations)
  (rewrites
    (rule prepare (head (metta-equation
      (tptp-read-v1:prepare (tptp-lex-v1:lowered (bnf-v1:document ?entries ?span)
        ?environment ?origins (tptp-ebnf-v1:document ?official ?official-span) ?dot))
      (tptp-read-v1:source-names ?official (tptp-read-v1:lexicals ?entries)))) (body))
    (rule source-names-empty (head (metta-equation
      (tptp-read-v1:source-names (tptp-ebnf-v1:entries-nil) ?tail) ?tail)) (body))
    (rule source-names-syntax (head (metta-equation
      (tptp-read-v1:source-names (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:syntax-row ?name ?rhs ?span) ?rest) ?tail)
      (LCons (tptp-read-v1:source-name (bnf-v1:text->string (tptp-lex-v1:text ?name))
        (tptp-read-v1:syntax)) (tptp-read-v1:source-names ?rest ?tail)))) (body))
    (rule source-names-semantic (head (metta-equation
      (tptp-read-v1:source-names (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:semantic-row ?name ?rhs ?span) ?rest) ?tail)
      (tptp-read-v1:source-names ?rest ?tail))) (body))
    (rule source-names-token (head (metta-equation
      (tptp-read-v1:source-names (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:token-row ?name ?rhs ?span) ?rest) ?tail)
      (tptp-read-v1:source-names ?rest ?tail))) (body))
    (rule source-names-character (head (metta-equation
      (tptp-read-v1:source-names (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:character-row ?name ?rhs ?span) ?rest) ?tail)
      (tptp-read-v1:source-names ?rest ?tail))) (body))
    (rule source-names-comment (head (metta-equation
      (tptp-read-v1:source-names (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:comment ?text ?span) ?rest) ?tail)
      (tptp-read-v1:source-names ?rest ?tail))) (body))
    (rule source-names-blank (head (metta-equation
      (tptp-read-v1:source-names (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:blank ?span) ?rest) ?tail)
      (tptp-read-v1:source-names ?rest ?tail))) (body))
    (rule lexical-empty (head (metta-equation
      (tptp-read-v1:lexicals (bnf-v1:entries-nil)) LNil)) (body))
    (rule lexical-cons (head (metta-equation
      (tptp-read-v1:lexicals (bnf-v1:entries-cons (bnf-v1:rule ?name ?body ?span) ?tail))
      (LCons (tptp-read-v1:source-name
        (bnf-v1:text->string (tptp-syn-v1:token-name ?name))
        (tptp-read-v1:token (bnf-v1:text->string ?name))) (tptp-read-v1:lexicals ?tail)))) (body))
    (rule roots-empty (head (metta-equation
      (tptp-read-v1:roots ?context (EBNF:DerivationsNil)) LNil)) (body))
    (rule roots-cons (head (metta-equation
      (tptp-read-v1:roots ?context (EBNF:DerivationsCons ?head ?tail))
      (LCons (tptp-read-v1:node ?context ?head LNil)
        (tptp-read-v1:roots ?context ?tail)))) (body))
    (rule node-rule (head (metta-equation
      (tptp-read-v1:node ?context
        (EBNF:Rule ?name ?rule-span ?alternative ?alternative-span ?input ?elements) ?tail)
      (tptp-read-v1:rule (tptp-read-v1:kind ?context ?name)
        ?context ?name ?alternative ?input ?elements ?tail))) (body))
    (rule node-reference (head (metta-equation
      (tptp-read-v1:node ?context (EBNF:Reference ?name ?span ?input ?tree) ?tail)
      (tptp-read-v1:node ?context ?tree ?tail))) (body))
    (rule node-group (head (metta-equation
      (tptp-read-v1:node ?context (EBNF:Group ?occurrence ?span ?choice ?alt-span ?input ?elements) ?tail)
      (tptp-read-v1:elements ?context ?elements ?tail))) (body))
    (rule node-repetition (head (metta-equation
      (tptp-read-v1:node ?context (EBNF:Repetition ?kind ?occurrence ?span ?input ?iterations) ?tail)
      (tptp-read-v1:iterations ?context ?iterations ?tail))) (body))
    (rule node-absent (head (metta-equation
      (tptp-read-v1:node ?context (EBNF:OptionalAbsent ?occurrence ?span ?input) ?tail) ?tail)) (body))
    (rule node-present (head (metta-equation
      (tptp-read-v1:node ?context (EBNF:OptionalPresent ?occurrence ?span ?input ?body) ?tail)
      (tptp-read-v1:node ?context ?body ?tail))) (body))
    (rule node-literal (head (metta-equation
      (tptp-read-v1:node ?context (EBNF:Literal ?text ?span (EBNF:InputSpan ?start ?stop)) ?tail)
      (LCons (tptp-cst:literal ?start ?stop ?text) ?tail))) (body))
    (rule elements-empty (head (metta-equation
      (tptp-read-v1:elements ?context (EBNF:ElementsNil) ?tail) ?tail)) (body))
    (rule elements-cons (head (metta-equation
      (tptp-read-v1:elements ?context (EBNF:ElementsCons ?head ?rest) ?tail)
      (tptp-read-v1:node ?context ?head (tptp-read-v1:elements ?context ?rest ?tail)))) (body))
    (rule iterations-empty (head (metta-equation
      (tptp-read-v1:iterations ?context (EBNF:IterationsNil) ?tail) ?tail)) (body))
    (rule iterations-cons (head (metta-equation
      (tptp-read-v1:iterations ?context (EBNF:IterationsCons ?head ?rest) ?tail)
      (tptp-read-v1:node ?context ?head (tptp-read-v1:iterations ?context ?rest ?tail)))) (body))
    (rule kind (head (metta-equation
      (tptp-read-v1:kind ?context ?name)
      (tptp-read-v1:entry-kind (langdef:ground-equal ?name "#entry") ?context ?name))) (body))
    (rule kind-entry (head (metta-equation
      (tptp-read-v1:entry-kind LangDef:SameValue ?context ?name) (tptp-read-v1:entry))) (body))
    (rule kind-not-entry (head (metta-equation
      (tptp-read-v1:entry-kind LangDef:DifferentValue ?context ?name)
      (tptp-read-v1:layout-kind (langdef:ground-equal ?name "#layout") ?context ?name))) (body))
    (rule kind-layout (head (metta-equation
      (tptp-read-v1:layout-kind LangDef:SameValue ?context ?name) (tptp-read-v1:layout))) (body))
    (rule kind-not-layout (head (metta-equation
      (tptp-read-v1:layout-kind LangDef:DifferentValue ?context ?name)
      (tptp-read-v1:lookup ?context ?name))) (body))
    ; There is deliberately no unknown-name success branch. Input is an
    ; admitted derivation of the composed source grammar, not arbitrary nodes.
    (rule lookup-cons (head (metta-equation
      (tptp-read-v1:lookup (LCons (tptp-read-v1:source-name ?found ?kind) ?rest) ?name)
      (tptp-read-v1:lookup-next (langdef:ground-equal ?name ?found) ?kind ?rest ?name))) (body))
    (rule lookup-yes (head (metta-equation
      (tptp-read-v1:lookup-next LangDef:SameValue ?kind ?rest ?name) ?kind)) (body))
    (rule lookup-no (head (metta-equation
      (tptp-read-v1:lookup-next LangDef:DifferentValue ?original ?rest ?name)
      (tptp-read-v1:lookup ?rest ?name))) (body))
    (rule rule-entry (head (metta-equation
      (tptp-read-v1:rule (tptp-read-v1:entry) ?context ?name ?alt ?input ?elements ?tail)
      (tptp-read-v1:elements ?context ?elements ?tail))) (body))
    (rule rule-layout (head (metta-equation
      (tptp-read-v1:rule (tptp-read-v1:layout) ?context ?name ?alt ?input ?elements ?tail) ?tail)) (body))
    (rule rule-syntax (head (metta-equation
      (tptp-read-v1:rule (tptp-read-v1:syntax) ?context ?name ?alt
        (EBNF:InputSpan ?start ?stop) ?elements ?tail)
      (LCons (tptp-cst:spanned-node (tptp-cst:production ?name ?alt) ?start ?stop
        (tptp-read-v1:elements ?context ?elements LNil)) ?tail))) (body))
    ; The raw lexical reference excludes trailing layout. Its input span,
    ; rather than the wrapper's span, belongs to the resulting token.
    (rule rule-token (head (metta-equation
      (tptp-read-v1:rule (tptp-read-v1:token ?original) ?context ?name ?alt ?input
        (EBNF:ElementsCons (EBNF:Reference ?raw ?span (EBNF:InputSpan ?start ?stop) ?tree)
          (EBNF:ElementsCons (EBNF:Reference "#layout" ?layout-span ?layout-input ?layout)
            (EBNF:ElementsNil))) ?tail)
      (tptp-read-v1:checked-token (langdef:ground-equal ?original ?raw)
        ?original ?start ?stop ?tree ?tail))) (body))
    (rule checked-token (head (metta-equation
      (tptp-read-v1:checked-token LangDef:SameValue ?original ?start ?stop ?tree ?tail)
      (LCons (tptp-cst:token ?original ?start ?stop
        (bnf-v1:text->string (tptp-read-v1:characters ?tree (bnf-v1:text-nil)))) ?tail))) (body))
    (rule characters-rule (head (metta-equation
      (tptp-read-v1:characters (EBNF:Rule ?name ?span ?alt ?alt-span ?input ?elements) ?tail)
      (tptp-read-v1:character-elements ?elements ?tail))) (body))
    (rule characters-reference (head (metta-equation
      (tptp-read-v1:characters (EBNF:Reference ?name ?span ?input ?tree) ?tail)
      (tptp-read-v1:characters ?tree ?tail))) (body))
    (rule characters-group (head (metta-equation
      (tptp-read-v1:characters (EBNF:Group ?occurrence ?span ?choice ?alt-span ?input ?elements) ?tail)
      (tptp-read-v1:character-elements ?elements ?tail))) (body))
    (rule characters-repetition (head (metta-equation
      (tptp-read-v1:characters (EBNF:Repetition ?kind ?occurrence ?span ?input ?iterations) ?tail)
      (tptp-read-v1:character-iterations ?iterations ?tail))) (body))
    (rule characters-absent (head (metta-equation
      (tptp-read-v1:characters (EBNF:OptionalAbsent ?occurrence ?span ?input) ?tail) ?tail)) (body))
    (rule characters-present (head (metta-equation
      (tptp-read-v1:characters (EBNF:OptionalPresent ?occurrence ?span ?input ?body) ?tail)
      (tptp-read-v1:characters ?body ?tail))) (body))
    (rule characters-literal (head (metta-equation
      (tptp-read-v1:characters (EBNF:Literal ?text ?span ?input) ?tail)
      (bnf-v1:text-append (bnf-v1:string->text ?text) ?tail))) (body))
    (rule characters-lexical (head (metta-equation
      (tptp-read-v1:characters (EBNF:Lexical ?name ?label ?origin ?input ?scalar) ?tail)
      (bnf-v1:text-cons ?scalar ?tail))) (body))
    (rule character-elements-empty (head (metta-equation
      (tptp-read-v1:character-elements (EBNF:ElementsNil) ?tail) ?tail)) (body))
    (rule character-elements-cons (head (metta-equation
      (tptp-read-v1:character-elements (EBNF:ElementsCons ?head ?rest) ?tail)
      (tptp-read-v1:characters ?head (tptp-read-v1:character-elements ?rest ?tail)))) (body))
    (rule character-iterations-empty (head (metta-equation
      (tptp-read-v1:character-iterations (EBNF:IterationsNil) ?tail) ?tail)) (body))
    (rule character-iterations-cons (head (metta-equation
      (tptp-read-v1:character-iterations (EBNF:IterationsCons ?head ?rest) ?tail)
      (tptp-read-v1:characters ?head (tptp-read-v1:character-iterations ?rest ?tail)))) (body))))
