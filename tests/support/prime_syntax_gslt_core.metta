; A reusable syntax GSLT expressed solely as ordinary term declarations and
; rewrite/judgment rules.  It has no privileged lexer, token table, grammar
; record, start-category field, regex, or guest-language callback.
;
; The historical schema names the alphabet sort Codepoint.  This concrete
; instantiation supplies UTF-8 code units, preserving source bytes exactly.
; Unicode scalar decoding is not silently attributed to this layer.
;
; The central relation is:
;   (parse Grammar Codepoints Value Rest)
; Parsing is relational evaluation.  Complete parses are normal proofs of:
;   (parse Grammar Input Value nil)

(presentation SyntaxGSLTCoreV0
  (types
    (type Codepoint)
    (type Codepoints)
    (type Grammar)
    (type Value)
    (type Name)
    (type Goal))

  (terms
    (constructor nil Codepoints)
    (constructor cons Codepoint Codepoints Codepoints)
    (constructor cp Int Codepoint)
    (constructor eps Value Grammar)
    (constructor char Codepoint Grammar)
    (constructor any Grammar)
    (constructor class Name Grammar)
    (constructor lit Codepoints Grammar)
    (constructor alt Grammar Grammar Grammar)
    (constructor seq Grammar Grammar Grammar)
    (constructor left Grammar Grammar Grammar)
    (constructor right Grammar Grammar Grammar)
    (constructor node Name Grammar Grammar)
    (constructor ref Name Grammar)
    (constructor star Grammar Grammar)
    (constructor plus Grammar Grammar)
    (constructor opt Grammar Grammar)
    (constructor until Grammar Grammar Grammar)
    (constructor eof Grammar)
    (constructor pair Value Value Value)
    (constructor some Value Value)
    (constructor parse Grammar Codepoints Value Codepoints Goal)
    (constructor member Name Codepoint Goal)
    (constructor definition Name Grammar Goal))

  (equations)

  (rewrites
    (rule parse-eps
      (head (parse (eps ?value) ?input ?value ?input))
      (body))

    (rule parse-char
      (head (parse (char ?codepoint) (cons ?codepoint ?rest) ?codepoint ?rest))
      (body))

    (rule parse-any
      (head (parse any (cons ?codepoint ?rest) ?codepoint ?rest))
      (body))

    (rule parse-class
      (head (parse (class ?class) (cons ?codepoint ?rest) ?codepoint ?rest))
      (body
        (member ?class ?codepoint)))

    (rule parse-lit-empty
      (head (parse (lit nil) ?input nil ?input))
      (body))

    (rule parse-lit-cons
      (head
        (parse
          (lit (cons ?codepoint ?more))
          (cons ?codepoint ?input-rest)
          (cons ?codepoint ?value-rest)
          ?final-rest))
      (body
        (parse (lit ?more) ?input-rest ?value-rest ?final-rest)))

    (rule parse-alt-left
      (head (parse (alt ?left ?right) ?input ?value ?rest))
      (body
        (parse ?left ?input ?value ?rest)))

    (rule parse-alt-right
      (head (parse (alt ?left ?right) ?input ?value ?rest))
      (body
        (parse ?right ?input ?value ?rest)))

    (rule parse-seq
      (head (parse (seq ?first ?second) ?input (pair ?a ?b) ?rest))
      (body
        (parse ?first ?input ?a ?middle)
        (parse ?second ?middle ?b ?rest)))

    (rule parse-left
      (head (parse (left ?first ?second) ?input ?a ?rest))
      (body
        (parse ?first ?input ?a ?middle)
        (parse ?second ?middle ?ignored ?rest)))

    (rule parse-right
      (head (parse (right ?first ?second) ?input ?b ?rest))
      (body
        (parse ?first ?input ?ignored ?middle)
        (parse ?second ?middle ?b ?rest)))

    (rule parse-node
      (head (parse (node ?label ?grammar) ?input (node ?label ?value) ?rest))
      (body
        (parse ?grammar ?input ?value ?rest)))

    (rule parse-ref
      (head (parse (ref ?name) ?input ?value ?rest))
      (body
        (definition ?name ?grammar)
        (parse ?grammar ?input ?value ?rest)))

    (rule parse-star-empty
      (head (parse (star ?grammar) ?input nil ?input))
      (body))

    (rule parse-star-cons
      (head (parse (star ?grammar) ?input (cons ?value ?values) ?rest))
      (body
        (parse ?grammar ?input ?value ?middle)
        (parse (star ?grammar) ?middle ?values ?rest)))

    (rule parse-plus
      (head (parse (plus ?grammar) ?input (cons ?value ?values) ?rest))
      (body
        (parse ?grammar ?input ?value ?middle)
        (parse (star ?grammar) ?middle ?values ?rest)))

    (rule parse-opt-none
      (head (parse (opt ?grammar) ?input none ?input))
      (body))

    (rule parse-opt-some
      (head (parse (opt ?grammar) ?input (some ?value) ?rest))
      (body
        (parse ?grammar ?input ?value ?rest)))

    ; Parse zero or more ITEM values, stopping by consuming END.
    ; END is tried first, so a delimiter is recognized before ITEM can consume it.
    (rule parse-until-end
      (head (parse (until ?end ?item) ?input nil ?rest))
      (body
        (parse ?end ?input ?ignored ?rest)))

    (rule parse-until-item
      (head (parse (until ?end ?item) ?input (cons ?value ?values) ?rest))
      (body
        (parse ?item ?input ?value ?middle)
        (parse (until ?end ?item) ?middle ?values ?rest)))

    (rule parse-eof
      (head (parse eof nil eof nil))
      (body))))
