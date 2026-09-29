; Scannerless composition reference for the pinned official syntax and regex
; rows. Produces the existing BNF grammar-input carrier. The original official
; document remains the source/semantic ledger held by the caller.
;
; Source-adjacent literal pieces are joined within words or operator runs;
; punctuation delimiters stay separate. Layout is added only after complete
; literal tokens or lexical-category references, never inside a regex row.
; This is not yet a maximal-token scanner or the public TPTP value projection.
; Requires the lexical adapter and common BNF/EBNF codec sources.
(gslt-presentation-v1 TptpOfficialSyntaxToEbnfV1
  (signature
    (operator metta-equation 2) (operator let 3)
    (operator < 2) (operator langdef:ground-equal 2)
    (operator bnf-v1:string->text 1) (operator bnf-v1:text-append 2)
    (operator tptp-lex-v1:text 1) (operator tptp-lex-v1:span 1)
    (operator tptp-lex-v1:lowered 5)
    (operator tptp-syn-v1:compose 1)
    (operator tptp-syn-v1:context 2)
    (operator tptp-syn-v1:macros 1)
    (operator tptp-syn-v1:macro-name 2)
    (operator tptp-syn-v1:member 2)
    (operator tptp-syn-v1:member-next 3)
    (operator LNil 0) (operator LCons 2)
    (operator tptp-syn-v1:rows 2)
    (operator tptp-syn-v1:expression 2)
    (operator tptp-syn-v1:alternatives 2)
    (operator tptp-syn-v1:alternative 2)
    (operator tptp-syn-v1:elements 2)
    (operator tptp-syn-v1:reference 3)
    (operator tptp-syn-v1:repeat 3)
    (operator tptp-syn-v1:lexical-name 2)
    (operator tptp-syn-v1:lexical-name-next 3)
    (operator tptp-syn-v1:reference-name 2)
    (operator tptp-syn-v1:token-name 1)
    (operator tptp-syn-v1:wrappers 1)
    (operator tptp-syn-v1:append 2)
    (operator tptp-syn-v1:literal-start 4)
    (operator tptp-syn-v1:literal-run 6)
    (operator tptp-syn-v1:literal-next 9)
    (operator tptp-syn-v1:literal-end 5)
    (operator tptp-syn-v1:literal-adjoin 6)
    (operator tptp-syn-v1:joinable 4)
    (operator tptp-syn-v1:kind 1)
    (operator tptp-syn-v1:kind-delimiter 2)
    (operator tptp-syn-v1:kind-word 1)
    (operator tptp-syn-v1:or 2)
    (operator tptp-syn-v1:and 2)
    (operator tptp-syn-v1:eq 2)
    (operator tptp-syn-v1:eq-result 1)
    (operator tptp-syn-v1:support 0)
    (operator tptp-syn-v1:layout-environment 1)
    (operator tptp-syn-v1:one-alternative 2)
    (operator bnf-v1:grammar-input 2)
    (operator bnf-v1:grammar-authority 2)
    (operator bnf-v1:start 1)
    (operator bnf-v1:document 2)
    (operator bnf-v1:source-span 2)
    (operator bnf-v1:entries-nil 0) (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternatives-nil 0) (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:elements-nil 0) (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:reference 2) (operator bnf-v1:literal 2)
    (operator bnf-v1:text-nil 0) (operator bnf-v1:text-cons 2)
    (operator ebnf-v1:zero-or-more 2) (operator ebnf-v1:group 2)
    (operator bnf-v1:lexical-environment 1)
    (operator bnf-v1:lexical-declarations-cons 2)
    (operator bnf-v1:lexical-declaration 5)
    (operator bnf-v1:lexical-origin 2)
    (operator bnf-v1:lexical-points 1)
    (operator bnf-v1:scalars-nil 0) (operator bnf-v1:scalars-cons 2)
    (operator tptp-ebnf-v1:document 2)
    (operator tptp-ebnf-v1:entries-nil 0) (operator tptp-ebnf-v1:entries-cons 2)
    (operator tptp-ebnf-v1:syntax-row 3) (operator tptp-ebnf-v1:semantic-row 3)
    (operator tptp-ebnf-v1:token-row 3) (operator tptp-ebnf-v1:character-row 3)
    (operator tptp-ebnf-v1:comment 2) (operator tptp-ebnf-v1:blank 1)
    (operator tptp-ebnf-v1:grammar-choice 2)
    (operator tptp-ebnf-v1:grammar-sequences-nil 0) (operator tptp-ebnf-v1:grammar-sequences-cons 2)
    (operator tptp-ebnf-v1:grammar-sequence 2)
    (operator tptp-ebnf-v1:grammar-elements-nil 0) (operator tptp-ebnf-v1:grammar-elements-cons 2)
    (operator tptp-ebnf-v1:grammar-reference 3) (operator tptp-ebnf-v1:grammar-literal 2)
    (operator tptp-ebnf-v1:grammar-once 0) (operator tptp-ebnf-v1:grammar-zero-or-more 0)
    (operator tptp-ebnf-v1:text-cons 2) (operator tptp-ebnf-v1:text-nil 0)
    (operator tptp-ebnf-v1:source-span 2))
  (equations)
  (rewrites
    (rule tptp-syn-compose (head (metta-equation
      (tptp-syn-v1:compose (tptp-lex-v1:lowered (bnf-v1:document ?lexical ?lspan) ?environment ?origins
        (tptp-ebnf-v1:document ?entries ?span) ?dot))
      (bnf-v1:grammar-input
        (bnf-v1:document
          (tptp-syn-v1:append (tptp-syn-v1:rows ?entries (tptp-syn-v1:context ?lexical (tptp-syn-v1:macros ?entries)))
            (tptp-syn-v1:append (tptp-syn-v1:wrappers ?lexical)
              (tptp-syn-v1:append ?lexical (tptp-syn-v1:support))))
          (tptp-lex-v1:span ?span))
        (bnf-v1:grammar-authority (bnf-v1:start (bnf-v1:string->text "#entry"))
          (tptp-syn-v1:layout-environment ?environment))))) (body))
    (rule tptp-syn-rows-nil (head (metta-equation
      (tptp-syn-v1:rows (tptp-ebnf-v1:entries-nil) ?lex) (bnf-v1:entries-nil))) (body))
    (rule tptp-syn-rows-syntax (head (metta-equation
      (tptp-syn-v1:rows (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:syntax-row ?name ?rhs ?span) ?tail) ?lex)
      (bnf-v1:entries-cons (bnf-v1:rule (tptp-lex-v1:text ?name)
        (tptp-syn-v1:expression ?rhs ?lex) (tptp-lex-v1:span ?span)) (tptp-syn-v1:rows ?tail ?lex)))) (body))
    (rule tptp-syn-rows-semantic (head (metta-equation
      (tptp-syn-v1:rows (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:semantic-row ?name ?rhs ?span) ?tail) ?lex)
      (tptp-syn-v1:rows ?tail ?lex))) (body))
    (rule tptp-syn-rows-token (head (metta-equation
      (tptp-syn-v1:rows (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:token-row ?name ?rhs ?span) ?tail) ?lex)
      (tptp-syn-v1:rows ?tail ?lex))) (body))
    (rule tptp-syn-rows-character (head (metta-equation
      (tptp-syn-v1:rows (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:character-row ?name ?rhs ?span) ?tail) ?lex)
      (tptp-syn-v1:rows ?tail ?lex))) (body))
    (rule tptp-syn-rows-comment (head (metta-equation
      (tptp-syn-v1:rows (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:comment ?text ?span) ?tail) ?lex)
      (tptp-syn-v1:rows ?tail ?lex))) (body))
    (rule tptp-syn-rows-blank (head (metta-equation
      (tptp-syn-v1:rows (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:blank ?span) ?tail) ?lex)
      (tptp-syn-v1:rows ?tail ?lex))) (body))
    (rule tptp-syn-expression (head (metta-equation
      (tptp-syn-v1:expression (tptp-ebnf-v1:grammar-choice ?alternatives ?span) ?lex)
      (bnf-v1:expression (tptp-syn-v1:alternatives ?alternatives ?lex) (tptp-lex-v1:span ?span)))) (body))
    (rule tptp-syn-alternatives-nil (head (metta-equation
      (tptp-syn-v1:alternatives (tptp-ebnf-v1:grammar-sequences-nil) ?lex) (bnf-v1:alternatives-nil))) (body))
    (rule tptp-syn-alternatives-cons (head (metta-equation
      (tptp-syn-v1:alternatives (tptp-ebnf-v1:grammar-sequences-cons ?head ?tail) ?lex)
      (bnf-v1:alternatives-cons (tptp-syn-v1:alternative ?head ?lex) (tptp-syn-v1:alternatives ?tail ?lex)))) (body))
    (rule tptp-syn-alternative (head (metta-equation
      (tptp-syn-v1:alternative (tptp-ebnf-v1:grammar-sequence ?elements ?span) ?lex)
      (bnf-v1:alternative (tptp-syn-v1:elements ?elements ?lex) (tptp-lex-v1:span ?span)))) (body))
    (rule tptp-syn-elements-nil (head (metta-equation
      (tptp-syn-v1:elements (tptp-ebnf-v1:grammar-elements-nil) ?lex) (bnf-v1:elements-nil))) (body))
    (rule tptp-syn-elements-reference (head (metta-equation
      (tptp-syn-v1:elements (tptp-ebnf-v1:grammar-elements-cons
        (tptp-ebnf-v1:grammar-reference ?name ?repetition ?span) ?tail) ?lex)
      (bnf-v1:elements-cons
        (tptp-syn-v1:repeat ?repetition (tptp-syn-v1:reference (tptp-lex-v1:text ?name) ?lex ?span) ?span)
        (tptp-syn-v1:elements ?tail ?lex)))) (body))
    (rule tptp-syn-elements-literal (head (metta-equation
      (tptp-syn-v1:elements (tptp-ebnf-v1:grammar-elements-cons
        (tptp-ebnf-v1:grammar-literal ?text ?span) ?tail) ?lex)
      (tptp-syn-v1:literal-start (tptp-lex-v1:text ?text) ?span ?tail ?lex))) (body))
    (rule tptp-syn-literal-start (head (metta-equation
      (tptp-syn-v1:literal-start (bnf-v1:text-cons ?cp ?rest)
        (tptp-ebnf-v1:source-span ?start ?end) ?tail ?lex)
      (tptp-syn-v1:literal-run (bnf-v1:text-cons ?cp ?rest) ?start ?end (tptp-syn-v1:kind ?cp) ?tail ?lex))) (body))
    (rule tptp-syn-literal-run-empty (head (metta-equation
      (tptp-syn-v1:literal-run ?text ?start ?end ?kind (tptp-ebnf-v1:grammar-elements-nil) ?lex)
      (tptp-syn-v1:literal-end ?text ?start ?end (tptp-ebnf-v1:grammar-elements-nil) ?lex))) (body))
    (rule tptp-syn-literal-run-reference (head (metta-equation
      (tptp-syn-v1:literal-run ?text ?start ?end ?kind
        (tptp-ebnf-v1:grammar-elements-cons (tptp-ebnf-v1:grammar-reference ?name ?repeat
          (tptp-ebnf-v1:source-span ?next-start ?next-end)) ?tail) ?lex)
      (tptp-syn-v1:literal-adjoin
        (tptp-syn-v1:and (tptp-syn-v1:eq ?end ?next-start)
          (tptp-syn-v1:macro-name (tptp-lex-v1:text ?name) ?lex)) ?text ?start ?end
        (tptp-ebnf-v1:grammar-elements-cons (tptp-ebnf-v1:grammar-reference ?name ?repeat
          (tptp-ebnf-v1:source-span ?next-start ?next-end)) ?tail) ?lex))) (body))
    ; A ::: macro is not a token. In the official ~<vline> spelling it
    ; completes the same operator; no layout boundary is introduced inside it.
    (rule tptp-syn-literal-adjoin-macro (head (metta-equation
      (tptp-syn-v1:literal-adjoin True ?text ?start ?end ?tail ?lex)
      (bnf-v1:elements-cons (bnf-v1:literal ?text (bnf-v1:source-span ?start ?end))
        (tptp-syn-v1:elements ?tail ?lex)))) (body))
    (rule tptp-syn-literal-adjoin-boundary (head (metta-equation
      (tptp-syn-v1:literal-adjoin False ?text ?start ?end ?tail ?lex)
      (tptp-syn-v1:literal-end ?text ?start ?end ?tail ?lex))) (body))
    (rule tptp-syn-literal-run-literal (head (metta-equation
      (tptp-syn-v1:literal-run ?text ?start ?end ?kind
        (tptp-ebnf-v1:grammar-elements-cons
          (tptp-ebnf-v1:grammar-literal (tptp-ebnf-v1:text-cons ?cp ?rest)
            (tptp-ebnf-v1:source-span ?next-start ?next-end)) ?tail) ?lex)
      (tptp-syn-v1:literal-next
        (tptp-syn-v1:joinable ?kind (tptp-syn-v1:kind ?cp) ?end ?next-start)
        ?text ?start ?end ?kind (tptp-ebnf-v1:text-cons ?cp ?rest)
        (tptp-ebnf-v1:source-span ?next-start ?next-end) ?tail ?lex))) (body))
    (rule tptp-syn-literal-next-join (head (metta-equation
      (tptp-syn-v1:literal-next True ?text ?start ?end ?kind ?next
        (tptp-ebnf-v1:source-span ?next-start ?next-end) ?tail ?lex)
      (tptp-syn-v1:literal-run (bnf-v1:text-append ?text (tptp-lex-v1:text ?next))
        ?start ?next-end ?kind ?tail ?lex))) (body))
    (rule tptp-syn-literal-next-flush (head (metta-equation
      (tptp-syn-v1:literal-next False ?text ?start ?end ?kind ?next ?span ?tail ?lex)
      (tptp-syn-v1:literal-end ?text ?start ?end
        (tptp-ebnf-v1:grammar-elements-cons (tptp-ebnf-v1:grammar-literal ?next ?span) ?tail) ?lex))) (body))
    (rule tptp-syn-literal-end (head (metta-equation
      (tptp-syn-v1:literal-end ?text ?start ?end ?tail ?lex)
      (bnf-v1:elements-cons (bnf-v1:literal ?text (bnf-v1:source-span ?start ?end))
        (bnf-v1:elements-cons (bnf-v1:reference (bnf-v1:string->text "#layout") (bnf-v1:source-span ?start ?end))
          (tptp-syn-v1:elements ?tail ?lex))))) (body))
    (rule tptp-syn-joinable-isolated (head (metta-equation
      (tptp-syn-v1:joinable isolated ?next ?end ?start) False)) (body))
    (rule tptp-syn-joinable-word (head (metta-equation
      (tptp-syn-v1:joinable word ?next ?end ?start)
      (tptp-syn-v1:and (tptp-syn-v1:eq word ?next) (tptp-syn-v1:eq ?end ?start)))) (body))
    (rule tptp-syn-joinable-operator (head (metta-equation
      (tptp-syn-v1:joinable operator ?next ?end ?start)
      (tptp-syn-v1:and (tptp-syn-v1:eq operator ?next) (tptp-syn-v1:eq ?end ?start)))) (body))
    (rule tptp-syn-reference (head (metta-equation
      (tptp-syn-v1:reference ?name (tptp-syn-v1:context ?lex ?macros) ?span)
      (bnf-v1:reference (tptp-syn-v1:reference-name (tptp-syn-v1:lexical-name ?name ?lex) ?name)
        (tptp-lex-v1:span ?span)))) (body))
    (rule tptp-syn-reference-syntax (head (metta-equation
      (tptp-syn-v1:reference-name False ?name) ?name)) (body))
    (rule tptp-syn-reference-token (head (metta-equation
      (tptp-syn-v1:reference-name True ?name) (tptp-syn-v1:token-name ?name))) (body))
    (rule tptp-syn-token-name (head (metta-equation
      (tptp-syn-v1:token-name ?name) (bnf-v1:text-append (bnf-v1:string->text "#token:") ?name))) (body))
    (rule tptp-syn-lexical-name-nil (head (metta-equation
      (tptp-syn-v1:lexical-name ?name (bnf-v1:entries-nil)) False)) (body))
    (rule tptp-syn-lexical-name-cons (head (metta-equation
      (tptp-syn-v1:lexical-name ?name (bnf-v1:entries-cons (bnf-v1:rule ?head ?rhs ?span) ?tail))
      (tptp-syn-v1:lexical-name-next (tptp-syn-v1:eq ?name ?head) ?name ?tail))) (body))
    (rule tptp-syn-lexical-name-found (head (metta-equation
      (tptp-syn-v1:lexical-name-next True ?name ?tail) True)) (body))
    (rule tptp-syn-lexical-name-more (head (metta-equation
      (tptp-syn-v1:lexical-name-next False ?name ?tail) (tptp-syn-v1:lexical-name ?name ?tail))) (body))
    (rule tptp-syn-macros-empty (head (metta-equation
      (tptp-syn-v1:macros (tptp-ebnf-v1:entries-nil)) LNil)) (body))
    (rule tptp-syn-macros-character (head (metta-equation
      (tptp-syn-v1:macros (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:character-row ?name ?rhs ?span) ?tail))
      (LCons (tptp-lex-v1:text ?name) (tptp-syn-v1:macros ?tail)))) (body))
    (rule tptp-syn-macros-syntax (head (metta-equation
      (tptp-syn-v1:macros (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:syntax-row ?name ?rhs ?span) ?tail))
      (tptp-syn-v1:macros ?tail))) (body))
    (rule tptp-syn-macros-semantic (head (metta-equation
      (tptp-syn-v1:macros (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:semantic-row ?name ?rhs ?span) ?tail))
      (tptp-syn-v1:macros ?tail))) (body))
    (rule tptp-syn-macros-token (head (metta-equation
      (tptp-syn-v1:macros (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:token-row ?name ?rhs ?span) ?tail))
      (tptp-syn-v1:macros ?tail))) (body))
    (rule tptp-syn-macros-comment (head (metta-equation
      (tptp-syn-v1:macros (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:comment ?text ?span) ?tail))
      (tptp-syn-v1:macros ?tail))) (body))
    (rule tptp-syn-macros-blank (head (metta-equation
      (tptp-syn-v1:macros (tptp-ebnf-v1:entries-cons (tptp-ebnf-v1:blank ?span) ?tail))
      (tptp-syn-v1:macros ?tail))) (body))
    (rule tptp-syn-macro-name (head (metta-equation
      (tptp-syn-v1:macro-name ?name (tptp-syn-v1:context ?lex ?macros))
      (tptp-syn-v1:member ?name ?macros))) (body))
    (rule tptp-syn-member-empty (head (metta-equation
      (tptp-syn-v1:member ?name LNil) False)) (body))
    (rule tptp-syn-member-cons (head (metta-equation
      (tptp-syn-v1:member ?name (LCons ?head ?tail))
      (tptp-syn-v1:member-next (tptp-syn-v1:eq ?name ?head) ?name ?tail))) (body))
    (rule tptp-syn-member-found (head (metta-equation
      (tptp-syn-v1:member-next True ?name ?tail) True)) (body))
    (rule tptp-syn-member-more (head (metta-equation
      (tptp-syn-v1:member-next False ?name ?tail) (tptp-syn-v1:member ?name ?tail))) (body))
    (rule tptp-syn-once (head (metta-equation
      (tptp-syn-v1:repeat (tptp-ebnf-v1:grammar-once) ?element ?span) ?element)) (body))
    (rule tptp-syn-star (head (metta-equation
      (tptp-syn-v1:repeat (tptp-ebnf-v1:grammar-zero-or-more) ?element ?span)
      (ebnf-v1:zero-or-more ?element (tptp-lex-v1:span ?span)))) (body))
    (rule tptp-syn-wrappers-nil (head (metta-equation
      (tptp-syn-v1:wrappers (bnf-v1:entries-nil)) (bnf-v1:entries-nil))) (body))
    (rule tptp-syn-wrappers-cons (head (metta-equation
      (tptp-syn-v1:wrappers (bnf-v1:entries-cons (bnf-v1:rule ?name ?rhs ?span) ?tail))
      (bnf-v1:entries-cons (bnf-v1:rule (tptp-syn-v1:token-name ?name)
        (tptp-syn-v1:one-alternative (bnf-v1:elements-cons (bnf-v1:reference ?name ?span)
          (bnf-v1:elements-cons (bnf-v1:reference (bnf-v1:string->text "#layout") ?span) (bnf-v1:elements-nil))) ?span) ?span)
        (tptp-syn-v1:wrappers ?tail)))) (body))
    (rule tptp-syn-append-nil (head (metta-equation
      (tptp-syn-v1:append (bnf-v1:entries-nil) ?tail) ?tail)) (body))
    (rule tptp-syn-append-cons (head (metta-equation
      (tptp-syn-v1:append (bnf-v1:entries-cons ?head ?rest) ?tail)
      (bnf-v1:entries-cons ?head (tptp-syn-v1:append ?rest ?tail)))) (body))
    (rule tptp-syn-one-alternative (head (metta-equation
      (tptp-syn-v1:one-alternative ?elements ?span)
      (bnf-v1:expression (bnf-v1:alternatives-cons (bnf-v1:alternative ?elements ?span) (bnf-v1:alternatives-nil)) ?span))) (body))
    (rule tptp-syn-support (head (metta-equation
      (tptp-syn-v1:support)
      (let ?span (bnf-v1:source-span 0 0)
        (bnf-v1:entries-cons
          (bnf-v1:rule (bnf-v1:string->text "#entry")
            (tptp-syn-v1:one-alternative
              (bnf-v1:elements-cons (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
                (bnf-v1:elements-cons (bnf-v1:reference (bnf-v1:string->text "TPTP_file") ?span) (bnf-v1:elements-nil))) ?span) ?span)
          (bnf-v1:entries-cons
            (bnf-v1:rule (bnf-v1:string->text "#layout")
              (tptp-syn-v1:one-alternative
                (bnf-v1:elements-cons
                  (ebnf-v1:zero-or-more
                    (ebnf-v1:group (bnf-v1:expression
                      (bnf-v1:alternatives-cons
                        (bnf-v1:alternative (bnf-v1:elements-cons (bnf-v1:reference (bnf-v1:string->text "#white") ?span) (bnf-v1:elements-nil)) ?span)
                        (bnf-v1:alternatives-cons
                          (bnf-v1:alternative (bnf-v1:elements-cons (bnf-v1:reference (bnf-v1:string->text "comment") ?span) (bnf-v1:elements-nil)) ?span)
                          (bnf-v1:alternatives-nil))) ?span) ?span) ?span)
                  (bnf-v1:elements-nil)) ?span) ?span)
            (bnf-v1:entries-nil)))))) (body))
    (rule tptp-syn-layout-environment (head (metta-equation
      (tptp-syn-v1:layout-environment (bnf-v1:lexical-environment ?decls))
      (bnf-v1:lexical-environment (bnf-v1:lexical-declarations-cons
        (bnf-v1:lexical-declaration (bnf-v1:string->text "#white") "TptpLayoutScalar"
          (bnf-v1:lexical-points (bnf-v1:scalars-cons 9 (bnf-v1:scalars-cons 10
            (bnf-v1:scalars-cons 11 (bnf-v1:scalars-cons 12 (bnf-v1:scalars-cons 13
              (bnf-v1:scalars-cons 32 (bnf-v1:scalars-nil)))))))) "tptp-layout-scalar"
          (bnf-v1:lexical-origin "tptp-scannerless-layout-v1" 0)) ?decls)))) (body))
    (rule tptp-syn-or-true (head (metta-equation (tptp-syn-v1:or True ?b) True)) (body))
    (rule tptp-syn-or-false (head (metta-equation (tptp-syn-v1:or False ?b) ?b)) (body))
    (rule tptp-syn-and-true (head (metta-equation (tptp-syn-v1:and True ?b) ?b)) (body))
    (rule tptp-syn-and-false (head (metta-equation (tptp-syn-v1:and False ?b) False)) (body))
    (rule tptp-syn-eq (head (metta-equation
      (tptp-syn-v1:eq ?a ?b) (tptp-syn-v1:eq-result (langdef:ground-equal ?a ?b)))) (body))
    (rule tptp-syn-eq-yes (head (metta-equation (tptp-syn-v1:eq-result LangDef:SameValue) True)) (body))
    (rule tptp-syn-eq-no (head (metta-equation (tptp-syn-v1:eq-result LangDef:DifferentValue) False)) (body))
    (rule tptp-syn-kind (head (metta-equation
      (tptp-syn-v1:kind ?cp)
      (tptp-syn-v1:kind-delimiter
        (tptp-syn-v1:or (tptp-syn-v1:eq ?cp 40) (tptp-syn-v1:or (tptp-syn-v1:eq ?cp 41)
          (tptp-syn-v1:or (tptp-syn-v1:eq ?cp 44) (tptp-syn-v1:or (tptp-syn-v1:eq ?cp 46)
            (tptp-syn-v1:or (tptp-syn-v1:eq ?cp 91) (tptp-syn-v1:eq ?cp 93)))))) ?cp))) (body))
    (rule tptp-syn-kind-isolated (head (metta-equation
      (tptp-syn-v1:kind-delimiter True ?cp) isolated)) (body))
    (rule tptp-syn-kind-classify (head (metta-equation
      (tptp-syn-v1:kind-delimiter False ?cp)
      (tptp-syn-v1:kind-word
        (tptp-syn-v1:or (tptp-syn-v1:and (< 47 ?cp) (< ?cp 58))
          (tptp-syn-v1:or (tptp-syn-v1:and (< 64 ?cp) (< ?cp 91))
            (tptp-syn-v1:or (tptp-syn-v1:and (< 96 ?cp) (< ?cp 123))
              (tptp-syn-v1:or (tptp-syn-v1:eq ?cp 95) (tptp-syn-v1:eq ?cp 36)))))))) (body))
    (rule tptp-syn-kind-word (head (metta-equation (tptp-syn-v1:kind-word True) word)) (body))
    (rule tptp-syn-kind-operator (head (metta-equation (tptp-syn-v1:kind-word False) operator)) (body))))
