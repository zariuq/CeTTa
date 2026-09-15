; EBNF -> plain BNF, as an executed structural GSLT transformation.
; A fresh helper prefix is longer than every authored rule/reference/start
; name and lexical field. A binary counter distinguishes helper occurrences,
; even when edited nodes have identical spans. No unresolved name can become
; bound accidentally. Original names, entries and spans are left unchanged.
; Repetition preserves nullable iterations; it does not deduplicate epsilon
; derivations. Cyclic result forests require an explicit observation policy.
; Repetition helpers are left recursive. Source iteration order is restored
; by the authored derivation projector, not by the parser or a bag quotient.
(gslt-presentation-v1 EbnfLoweringV1
  (signature
    (operator metta-equation 2)
    (operator bnf-v1:text-nil 0)
    (operator ebnf-v1:text-width 1)
    (operator ebnf-v1:z 0)
    (operator bnf-v1:text-cons 2)
    (operator ebnf-v1:s 1)
    (operator ebnf-v1:max 2)
    (operator ebnf-v1:prefix 1)
    (operator ebnf-v1:next-bits 1)
    (operator bnf-v1:entries-nil 0)
    (operator ebnf-v1:width-entries 1)
    (operator bnf-v1:entries-cons 2)
    (operator ebnf-v1:width-entry 1)
    (operator bnf-v1:alternatives-nil 0)
    (operator ebnf-v1:width-alternatives 1)
    (operator bnf-v1:alternatives-cons 2)
    (operator ebnf-v1:width-alternative 1)
    (operator bnf-v1:elements-nil 0)
    (operator ebnf-v1:width-elements 1)
    (operator bnf-v1:elements-cons 2)
    (operator ebnf-v1:width-element 1)
    (operator bnf-v1:lexical-declarations-nil 0)
    (operator ebnf-v1:width-lexicals 1)
    (operator bnf-v1:lexical-declarations-cons 2)
    (operator ebnf-v1:width-lexical 1)
    (operator bnf-v1:rule 3)
    (operator ebnf-v1:width-expression 1)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:reference 2)
    (operator bnf-v1:literal 2)
    (operator ebnf-v1:group 2)
    (operator ebnf-v1:optional 2)
    (operator ebnf-v1:zero-or-more 2)
    (operator ebnf-v1:one-or-more 2)
    (operator bnf-v1:lexical-declaration 5)
    (operator bnf-v1:string->text 1)
    (operator bnf-v1:document 2)
    (operator bnf-v1:start 1)
    (operator bnf-v1:lexical-environment 1)
    (operator bnf-v1:grammar-authority 2)
    (operator ebnf-v1:lower 2)
    (operator ebnf-v1:origins-nil 0)
    (operator ebnf-v1:state 5)
    (operator ebnf-v1:rule-origin 2)
    (operator ebnf-v1:lower-entries 2)
    (operator ebnf-v1:finish 2)
    (operator ebnf-v1:result 2)
    (operator ebnf-v1:reverse-entries 2)
    (operator ebnf-v1:append-entries 2)
    (operator ebnf-v1:reverse-origins 2)
    (operator ebnf-v1:lowered 2)
    (operator ebnf-v1:origins-cons 2)
    (operator ebnf-v1:lower-entry 2)
    (operator ebnf-v1:after-entry 2)
    (operator ebnf-v1:join-entries 2)
    (operator ebnf-v1:lower-alternatives 2)
    (operator ebnf-v1:lower-alternative 2)
    (operator ebnf-v1:after-alternative 2)
    (operator ebnf-v1:join-alternatives 2)
    (operator ebnf-v1:lower-elements 2)
    (operator ebnf-v1:lower-element 2)
    (operator ebnf-v1:after-element 2)
    (operator ebnf-v1:join-elements 2)
    (operator ebnf-v1:lower-expression 2)
    (operator ebnf-v1:finish-rule 3)
    (operator ebnf-v1:finish-expression 2)
    (operator ebnf-v1:finish-alternative 2)
    (operator ebnf-v1:allocate-group 2)
    (operator bnf-v1:text-append 2)
    (operator ebnf-v1:install-helper 5)
    (operator ebnf-v1:allocate-unary 3)
    (operator ebnf-v1:unary-expression 4)
    (operator let 3)
    (operator ebnf-v1:helper-origin 4))
  (equations)
  (rewrites
    (rule width-empty
      (head (metta-equation
        (ebnf-v1:text-width (bnf-v1:text-nil))
        (ebnf-v1:z)))
      (body))
    (rule width-cons
      (head (metta-equation
        (ebnf-v1:text-width (bnf-v1:text-cons ?cp ?tail))
        (ebnf-v1:s (ebnf-v1:text-width ?tail))))
      (body))
    (rule max-left-zero
      (head (metta-equation
        (ebnf-v1:max (ebnf-v1:z) ?right)
        ?right))
      (body))
    (rule max-right-zero
      (head (metta-equation
        (ebnf-v1:max (ebnf-v1:s ?left) (ebnf-v1:z))
        (ebnf-v1:s ?left)))
      (body))
    (rule max-successors
      (head (metta-equation
        (ebnf-v1:max (ebnf-v1:s ?left) (ebnf-v1:s ?right))
        (ebnf-v1:s (ebnf-v1:max ?left ?right))))
      (body))
    (rule prefix-zero
      (head (metta-equation
        (ebnf-v1:prefix (ebnf-v1:z))
        (bnf-v1:text-cons 35 (bnf-v1:text-nil))))
      (body))
    (rule prefix-successor
      (head (metta-equation
        (ebnf-v1:prefix (ebnf-v1:s ?n))
        (bnf-v1:text-cons 35 (ebnf-v1:prefix ?n))))
      (body))
    (rule counter-empty
      (head (metta-equation
        (ebnf-v1:next-bits (bnf-v1:text-nil))
        (bnf-v1:text-cons 49 (bnf-v1:text-nil))))
      (body))
    (rule counter-zero
      (head (metta-equation
        (ebnf-v1:next-bits (bnf-v1:text-cons 48 ?tail))
        (bnf-v1:text-cons 49 ?tail)))
      (body))
    (rule counter-one
      (head (metta-equation
        (ebnf-v1:next-bits (bnf-v1:text-cons 49 ?tail))
        (bnf-v1:text-cons 48 (ebnf-v1:next-bits ?tail))))
      (body))
    (rule width-entries-empty
      (head (metta-equation
        (ebnf-v1:width-entries (bnf-v1:entries-nil))
        (ebnf-v1:z)))
      (body))
    (rule width-entries-cons
      (head (metta-equation
        (ebnf-v1:width-entries (bnf-v1:entries-cons ?head ?tail))
        (ebnf-v1:max (ebnf-v1:width-entry ?head) (ebnf-v1:width-entries ?tail))))
      (body))
    (rule width-alternatives-empty
      (head (metta-equation
        (ebnf-v1:width-alternatives (bnf-v1:alternatives-nil))
        (ebnf-v1:z)))
      (body))
    (rule width-alternatives-cons
      (head (metta-equation
        (ebnf-v1:width-alternatives (bnf-v1:alternatives-cons ?head ?tail))
        (ebnf-v1:max (ebnf-v1:width-alternative ?head) (ebnf-v1:width-alternatives ?tail))))
      (body))
    (rule width-elements-empty
      (head (metta-equation
        (ebnf-v1:width-elements (bnf-v1:elements-nil))
        (ebnf-v1:z)))
      (body))
    (rule width-elements-cons
      (head (metta-equation
        (ebnf-v1:width-elements (bnf-v1:elements-cons ?head ?tail))
        (ebnf-v1:max (ebnf-v1:width-element ?head) (ebnf-v1:width-elements ?tail))))
      (body))
    (rule width-lexicals-empty
      (head (metta-equation
        (ebnf-v1:width-lexicals (bnf-v1:lexical-declarations-nil))
        (ebnf-v1:z)))
      (body))
    (rule width-lexicals-cons
      (head (metta-equation
        (ebnf-v1:width-lexicals (bnf-v1:lexical-declarations-cons ?head ?tail))
        (ebnf-v1:max (ebnf-v1:width-lexical ?head) (ebnf-v1:width-lexicals ?tail))))
      (body))
    (rule width-rule
      (head (metta-equation
        (ebnf-v1:width-entry (bnf-v1:rule ?name ?expr ?span))
        (ebnf-v1:max (ebnf-v1:text-width ?name) (ebnf-v1:width-expression ?expr))))
      (body))
    (rule width-comment
      (head (metta-equation
        (ebnf-v1:width-entry (bnf-v1:comment ?text ?span))
        (ebnf-v1:z)))
      (body))
    (rule width-blank
      (head (metta-equation
        (ebnf-v1:width-entry (bnf-v1:blank ?span))
        (ebnf-v1:z)))
      (body))
    (rule width-expression
      (head (metta-equation
        (ebnf-v1:width-expression (bnf-v1:expression ?alts ?span))
        (ebnf-v1:width-alternatives ?alts)))
      (body))
    (rule width-alternative
      (head (metta-equation
        (ebnf-v1:width-alternative (bnf-v1:alternative ?elems ?span))
        (ebnf-v1:width-elements ?elems)))
      (body))
    (rule width-reference
      (head (metta-equation
        (ebnf-v1:width-element (bnf-v1:reference ?name ?span))
        (ebnf-v1:text-width ?name)))
      (body))
    (rule width-literal
      (head (metta-equation
        (ebnf-v1:width-element (bnf-v1:literal ?text ?span))
        (ebnf-v1:z)))
      (body))
    (rule width-group
      (head (metta-equation
        (ebnf-v1:width-element (ebnf-v1:group ?expr ?span))
        (ebnf-v1:width-expression ?expr)))
      (body))
    (rule width-optional
      (head (metta-equation
        (ebnf-v1:width-element (ebnf-v1:optional ?body ?span))
        (ebnf-v1:width-element ?body)))
      (body))
    (rule width-zero-or-more
      (head (metta-equation
        (ebnf-v1:width-element (ebnf-v1:zero-or-more ?body ?span))
        (ebnf-v1:width-element ?body)))
      (body))
    (rule width-one-or-more
      (head (metta-equation
        (ebnf-v1:width-element (ebnf-v1:one-or-more ?body ?span))
        (ebnf-v1:width-element ?body)))
      (body))
    (rule width-lexical
      (head (metta-equation
        (ebnf-v1:width-lexical (bnf-v1:lexical-declaration ?name ?klass ?matcher ?label ?origin))
        (ebnf-v1:max (ebnf-v1:text-width ?name) (ebnf-v1:max (ebnf-v1:text-width (bnf-v1:string->text ?klass)) (ebnf-v1:text-width (bnf-v1:string->text ?label))))))
      (body))
    (rule lower-document
      (head (metta-equation
        (ebnf-v1:lower (bnf-v1:document ?entries ?span) (bnf-v1:grammar-authority (bnf-v1:start ?start) (bnf-v1:lexical-environment ?lexicals)))
        (ebnf-v1:finish
  (ebnf-v1:lower-entries ?entries
    (ebnf-v1:state
      (ebnf-v1:prefix (ebnf-v1:max (ebnf-v1:text-width ?start)
        (ebnf-v1:max (ebnf-v1:width-entries ?entries) (ebnf-v1:width-lexicals ?lexicals))))
      (bnf-v1:text-cons 48 (bnf-v1:text-nil)) (bnf-v1:entries-nil) (ebnf-v1:origins-nil) ebnf-v1:outside-rule))
  ?span)))
      (body))
    (rule finish
      (head (metta-equation
        (ebnf-v1:finish (ebnf-v1:result ?entries (ebnf-v1:state ?prefix ?bits ?helpers ?origins ?origin)) ?span)
        (ebnf-v1:lowered
  (bnf-v1:document (ebnf-v1:append-entries ?entries (ebnf-v1:reverse-entries ?helpers (bnf-v1:entries-nil))) ?span)
  (ebnf-v1:reverse-origins ?origins (ebnf-v1:origins-nil)))))
      (body))
    (rule append-entries-empty
      (head (metta-equation
        (ebnf-v1:append-entries (bnf-v1:entries-nil) ?right)
        ?right))
      (body))
    (rule append-entries-cons
      (head (metta-equation
        (ebnf-v1:append-entries (bnf-v1:entries-cons ?head ?tail) ?right)
        (bnf-v1:entries-cons ?head (ebnf-v1:append-entries ?tail ?right))))
      (body))
    (rule reverse-entries-empty
      (head (metta-equation
        (ebnf-v1:reverse-entries (bnf-v1:entries-nil) ?acc)
        ?acc))
      (body))
    (rule reverse-entries-cons
      (head (metta-equation
        (ebnf-v1:reverse-entries (bnf-v1:entries-cons ?head ?tail) ?acc)
        (ebnf-v1:reverse-entries ?tail (bnf-v1:entries-cons ?head ?acc))))
      (body))
    (rule reverse-origins-empty
      (head (metta-equation
        (ebnf-v1:reverse-origins (ebnf-v1:origins-nil) ?acc)
        ?acc))
      (body))
    (rule reverse-origins-cons
      (head (metta-equation
        (ebnf-v1:reverse-origins (ebnf-v1:origins-cons ?head ?tail) ?acc)
        (ebnf-v1:reverse-origins ?tail (ebnf-v1:origins-cons ?head ?acc))))
      (body))
    (rule lower-entries-empty
      (head (metta-equation
        (ebnf-v1:lower-entries (bnf-v1:entries-nil) ?state)
        (ebnf-v1:result (bnf-v1:entries-nil) ?state)))
      (body))
    (rule lower-entries-cons
      (head (metta-equation
        (ebnf-v1:lower-entries (bnf-v1:entries-cons ?head ?tail) ?state)
        (ebnf-v1:after-entry (ebnf-v1:lower-entry ?head ?state) ?tail)))
      (body))
    (rule after-entry
      (head (metta-equation
        (ebnf-v1:after-entry (ebnf-v1:result ?head ?state) ?tail)
        (ebnf-v1:join-entries ?head (ebnf-v1:lower-entries ?tail ?state))))
      (body))
    (rule join-entries
      (head (metta-equation
        (ebnf-v1:join-entries ?head (ebnf-v1:result ?tail ?state))
        (ebnf-v1:result (bnf-v1:entries-cons ?head ?tail) ?state)))
      (body))
    (rule lower-alternatives-empty
      (head (metta-equation
        (ebnf-v1:lower-alternatives (bnf-v1:alternatives-nil) ?state)
        (ebnf-v1:result (bnf-v1:alternatives-nil) ?state)))
      (body))
    (rule lower-alternatives-cons
      (head (metta-equation
        (ebnf-v1:lower-alternatives (bnf-v1:alternatives-cons ?head ?tail) ?state)
        (ebnf-v1:after-alternative (ebnf-v1:lower-alternative ?head ?state) ?tail)))
      (body))
    (rule after-alternative
      (head (metta-equation
        (ebnf-v1:after-alternative (ebnf-v1:result ?head ?state) ?tail)
        (ebnf-v1:join-alternatives ?head (ebnf-v1:lower-alternatives ?tail ?state))))
      (body))
    (rule join-alternatives
      (head (metta-equation
        (ebnf-v1:join-alternatives ?head (ebnf-v1:result ?tail ?state))
        (ebnf-v1:result (bnf-v1:alternatives-cons ?head ?tail) ?state)))
      (body))
    (rule lower-elements-empty
      (head (metta-equation
        (ebnf-v1:lower-elements (bnf-v1:elements-nil) ?state)
        (ebnf-v1:result (bnf-v1:elements-nil) ?state)))
      (body))
    (rule lower-elements-cons
      (head (metta-equation
        (ebnf-v1:lower-elements (bnf-v1:elements-cons ?head ?tail) ?state)
        (ebnf-v1:after-element (ebnf-v1:lower-element ?head ?state) ?tail)))
      (body))
    (rule after-element
      (head (metta-equation
        (ebnf-v1:after-element (ebnf-v1:result ?head ?state) ?tail)
        (ebnf-v1:join-elements ?head (ebnf-v1:lower-elements ?tail ?state))))
      (body))
    (rule join-elements
      (head (metta-equation
        (ebnf-v1:join-elements ?head (ebnf-v1:result ?tail ?state))
        (ebnf-v1:result (bnf-v1:elements-cons ?head ?tail) ?state)))
      (body))
    (rule lower-entry-rule
      (head (metta-equation
        (ebnf-v1:lower-entry (bnf-v1:rule ?name ?expr ?span)
          (ebnf-v1:state ?prefix ?bits ?helpers ?origins ?previous-origin))
        (ebnf-v1:finish-rule ?name ?span
          (ebnf-v1:lower-expression ?expr
            (ebnf-v1:state ?prefix ?bits ?helpers ?origins
              (ebnf-v1:rule-origin ?name ?span))))))
      (body))
    (rule finish-rule
      (head (metta-equation
        (ebnf-v1:finish-rule ?name ?span (ebnf-v1:result ?expr ?state))
        (ebnf-v1:result (bnf-v1:rule ?name ?expr ?span) ?state)))
      (body))
    (rule lower-entry-comment
      (head (metta-equation
        (ebnf-v1:lower-entry (bnf-v1:comment ?text ?span) ?state)
        (ebnf-v1:result (bnf-v1:comment ?text ?span) ?state)))
      (body))
    (rule lower-entry-blank
      (head (metta-equation
        (ebnf-v1:lower-entry (bnf-v1:blank ?span) ?state)
        (ebnf-v1:result (bnf-v1:blank ?span) ?state)))
      (body))
    (rule lower-expression
      (head (metta-equation
        (ebnf-v1:lower-expression (bnf-v1:expression ?alts ?span) ?state)
        (ebnf-v1:finish-expression (ebnf-v1:lower-alternatives ?alts ?state) ?span)))
      (body))
    (rule finish-expression
      (head (metta-equation
        (ebnf-v1:finish-expression (ebnf-v1:result ?alts ?state) ?span)
        (ebnf-v1:result (bnf-v1:expression ?alts ?span) ?state)))
      (body))
    (rule lower-alternative
      (head (metta-equation
        (ebnf-v1:lower-alternative (bnf-v1:alternative ?elems ?span) ?state)
        (ebnf-v1:finish-alternative (ebnf-v1:lower-elements ?elems ?state) ?span)))
      (body))
    (rule finish-alternative
      (head (metta-equation
        (ebnf-v1:finish-alternative (ebnf-v1:result ?elems ?state) ?span)
        (ebnf-v1:result (bnf-v1:alternative ?elems ?span) ?state)))
      (body))
    (rule lower-reference
      (head (metta-equation
        (ebnf-v1:lower-element (bnf-v1:reference ?value ?span) ?state)
        (ebnf-v1:result (bnf-v1:reference ?value ?span) ?state)))
      (body))
    (rule lower-literal
      (head (metta-equation
        (ebnf-v1:lower-element (bnf-v1:literal ?value ?span) ?state)
        (ebnf-v1:result (bnf-v1:literal ?value ?span) ?state)))
      (body))
    (rule lower-group
      (head (metta-equation
        (ebnf-v1:lower-element (ebnf-v1:group ?expr ?span) ?state)
        (ebnf-v1:allocate-group (ebnf-v1:lower-expression ?expr ?state) ?span)))
      (body))
    (rule allocate-group
      (head (metta-equation
        (ebnf-v1:allocate-group (ebnf-v1:result ?expr (ebnf-v1:state ?prefix ?bits ?helpers ?origins ?origin)) ?span)
        (ebnf-v1:install-helper (bnf-v1:text-append ?prefix ?bits) group ?span ?expr
 (ebnf-v1:state ?prefix (ebnf-v1:next-bits ?bits) ?helpers ?origins ?origin))))
      (body))
    (rule lower-optional
      (head (metta-equation
        (ebnf-v1:lower-element (ebnf-v1:optional ?body ?span) ?state)
        (ebnf-v1:allocate-unary optional (ebnf-v1:lower-element ?body ?state) ?span)))
      (body))
    (rule lower-zero-or-more
      (head (metta-equation
        (ebnf-v1:lower-element (ebnf-v1:zero-or-more ?body ?span) ?state)
        (ebnf-v1:allocate-unary zero-or-more (ebnf-v1:lower-element ?body ?state) ?span)))
      (body))
    (rule lower-one-or-more
      (head (metta-equation
        (ebnf-v1:lower-element (ebnf-v1:one-or-more ?body ?span) ?state)
        (ebnf-v1:allocate-unary one-or-more (ebnf-v1:lower-element ?body ?state) ?span)))
      (body))
    (rule allocate-unary
      (head (metta-equation
        (ebnf-v1:allocate-unary ?kind (ebnf-v1:result ?body (ebnf-v1:state ?prefix ?bits ?helpers ?origins ?origin)) ?span)
        (let ?name (bnf-v1:text-append ?prefix ?bits)
  (ebnf-v1:install-helper ?name ?kind ?span
    (ebnf-v1:unary-expression ?kind ?body ?name ?span)
    (ebnf-v1:state ?prefix (ebnf-v1:next-bits ?bits) ?helpers ?origins ?origin)))))
      (body))
    (rule expression-optional
      (head (metta-equation
        (ebnf-v1:unary-expression optional ?body ?name ?span)
        (bnf-v1:expression (bnf-v1:alternatives-cons (bnf-v1:alternative (bnf-v1:elements-nil) ?span)
  (bnf-v1:alternatives-cons (bnf-v1:alternative (bnf-v1:elements-cons ?body (bnf-v1:elements-nil)) ?span) (bnf-v1:alternatives-nil))) ?span)))
      (body))
    (rule expression-zero-or-more
      (head (metta-equation
        (ebnf-v1:unary-expression zero-or-more ?body ?name ?span)
        (bnf-v1:expression (bnf-v1:alternatives-cons (bnf-v1:alternative (bnf-v1:elements-nil) ?span)
  (bnf-v1:alternatives-cons (bnf-v1:alternative (bnf-v1:elements-cons (bnf-v1:reference ?name ?span) (bnf-v1:elements-cons ?body (bnf-v1:elements-nil))) ?span) (bnf-v1:alternatives-nil))) ?span)))
      (body))
    (rule expression-one-or-more
      (head (metta-equation
        (ebnf-v1:unary-expression one-or-more ?body ?name ?span)
        (bnf-v1:expression (bnf-v1:alternatives-cons (bnf-v1:alternative (bnf-v1:elements-cons ?body (bnf-v1:elements-nil)) ?span)
  (bnf-v1:alternatives-cons (bnf-v1:alternative (bnf-v1:elements-cons (bnf-v1:reference ?name ?span) (bnf-v1:elements-cons ?body (bnf-v1:elements-nil))) ?span) (bnf-v1:alternatives-nil))) ?span)))
      (body))
    (rule install-helper
      (head (metta-equation
        (ebnf-v1:install-helper ?name ?kind ?span ?expr (ebnf-v1:state ?prefix ?bits ?helpers ?origins ?origin))
        (ebnf-v1:result (bnf-v1:reference ?name ?span)
  (ebnf-v1:state ?prefix ?bits
    (bnf-v1:entries-cons (bnf-v1:rule ?name ?expr ?span) ?helpers)
    (ebnf-v1:origins-cons (ebnf-v1:helper-origin ?name ?kind ?origin ?span) ?origins)
    ?origin))))
      (body))))
