; Ordered declaration/reference-occurrence view, not a grammar-language
; lowering. A group is traversed once, and a repetition operand is traversed
; once regardless of its eventual iterations. This view is consumed only by
; declaration validation, never by graph analysis, denotation, or parsing.
(gslt-presentation-v1 EbnfDeclarationViewV1
  (signature
    (operator metta-equation 2)
    (operator bnf-v1:document 2)
    (operator ebnf-v1:declaration-view 1)
    (operator ebnf-v1:declaration-entries 1)
    (operator bnf-v1:entries-nil 0)
    (operator bnf-v1:entries-cons 2)
    (operator ebnf-v1:declaration-entry 1)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:rule 3)
    (operator ebnf-v1:declaration-alts 1)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:alternatives-nil 0)
    (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator bnf-v1:elements-nil 0)
    (operator ebnf-v1:declaration-elements 1)
    (operator ebnf-v1:append-elements 2)
    (operator bnf-v1:elements-cons 2)
    (operator ebnf-v1:declaration-element 1)
    (operator bnf-v1:reference 2)
    (operator bnf-v1:literal 2)
    (operator ebnf-v1:group 2)
    (operator ebnf-v1:optional 2)
    (operator ebnf-v1:zero-or-more 2)
    (operator ebnf-v1:one-or-more 2))
  (equations)
  (rewrites
    (rule declaration-view
      (head (metta-equation
        (ebnf-v1:declaration-view (bnf-v1:document ?entries ?span))
        (bnf-v1:document (ebnf-v1:declaration-entries ?entries) ?span)))
      (body))
    (rule entries-empty
      (head (metta-equation
        (ebnf-v1:declaration-entries (bnf-v1:entries-nil))
        (bnf-v1:entries-nil)))
      (body))
    (rule entries-cons
      (head (metta-equation
        (ebnf-v1:declaration-entries (bnf-v1:entries-cons ?head ?tail))
        (bnf-v1:entries-cons (ebnf-v1:declaration-entry ?head) (ebnf-v1:declaration-entries ?tail))))
      (body))
    (rule entry-rule
      (head (metta-equation
        (ebnf-v1:declaration-entry (bnf-v1:rule ?name (bnf-v1:expression ?alts ?span) ?rule-span))
        (bnf-v1:rule ?name (bnf-v1:expression (bnf-v1:alternatives-cons (bnf-v1:alternative (ebnf-v1:declaration-alts ?alts) ?span) (bnf-v1:alternatives-nil)) ?span) ?rule-span)))
      (body))
    (rule entry-comment
      (head (metta-equation
        (ebnf-v1:declaration-entry (bnf-v1:comment ?text ?span))
        (bnf-v1:comment ?text ?span)))
      (body))
    (rule entry-blank
      (head (metta-equation
        (ebnf-v1:declaration-entry (bnf-v1:blank ?span))
        (bnf-v1:blank ?span)))
      (body))
    (rule alts-empty
      (head (metta-equation
        (ebnf-v1:declaration-alts (bnf-v1:alternatives-nil))
        (bnf-v1:elements-nil)))
      (body))
    (rule alts-cons
      (head (metta-equation
        (ebnf-v1:declaration-alts (bnf-v1:alternatives-cons (bnf-v1:alternative ?elements ?span) ?tail))
        (ebnf-v1:append-elements (ebnf-v1:declaration-elements ?elements) (ebnf-v1:declaration-alts ?tail))))
      (body))
    (rule elements-empty
      (head (metta-equation
        (ebnf-v1:declaration-elements (bnf-v1:elements-nil))
        (bnf-v1:elements-nil)))
      (body))
    (rule elements-cons
      (head (metta-equation
        (ebnf-v1:declaration-elements (bnf-v1:elements-cons ?head ?tail))
        (ebnf-v1:append-elements (ebnf-v1:declaration-element ?head) (ebnf-v1:declaration-elements ?tail))))
      (body))
    (rule append-empty
      (head (metta-equation
        (ebnf-v1:append-elements (bnf-v1:elements-nil) ?tail)
        ?tail))
      (body))
    (rule append-cons
      (head (metta-equation
        (ebnf-v1:append-elements (bnf-v1:elements-cons ?head ?rest) ?tail)
        (bnf-v1:elements-cons ?head (ebnf-v1:append-elements ?rest ?tail))))
      (body))
    (rule element-reference
      (head (metta-equation
        (ebnf-v1:declaration-element (bnf-v1:reference ?value ?span))
        (bnf-v1:elements-cons (bnf-v1:reference ?value ?span) (bnf-v1:elements-nil))))
      (body))
    (rule element-literal
      (head (metta-equation
        (ebnf-v1:declaration-element (bnf-v1:literal ?value ?span))
        (bnf-v1:elements-cons (bnf-v1:literal ?value ?span) (bnf-v1:elements-nil))))
      (body))
    (rule element-group
      (head (metta-equation
        (ebnf-v1:declaration-element (ebnf-v1:group (bnf-v1:expression ?alts ?expr-span) ?span))
        (ebnf-v1:declaration-alts ?alts)))
      (body))
    (rule element-optional
      (head (metta-equation
        (ebnf-v1:declaration-element (ebnf-v1:optional ?body ?span))
        (ebnf-v1:declaration-element ?body)))
      (body))
    (rule element-zero-or-more
      (head (metta-equation
        (ebnf-v1:declaration-element (ebnf-v1:zero-or-more ?body ?span))
        (ebnf-v1:declaration-element ?body)))
      (body))
    (rule element-one-or-more
      (head (metta-equation
        (ebnf-v1:declaration-element (ebnf-v1:one-or-more ?body ?span))
        (ebnf-v1:declaration-element ?body)))
      (body))))
