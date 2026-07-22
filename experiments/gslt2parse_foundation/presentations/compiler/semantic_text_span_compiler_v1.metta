; Semantics-directed lowering from syntax SIR to the regular language of
; flattened codepoint values consumed by a text-wrapper projection.  Wrapper
; roots are selected by their authored node label.  References remain
; namespaced references, so each definition is compiled once and shared.
;
; This is deliberately a certified fragment: constructors whose ParserPack
; value is not a codepoint text spine have no lowering rule.  Their absence
; makes the finite-Horn query fail closed instead of erasing value structure.

(gslt-presentation-v1 SemanticTextSpanCompilerV1
  (signature
    (operator compile-semantic-text-definition 2)
    (operator compile-semantic-text-grammar 2)
    (operator compile-semantic-text-lit 2)
    (operator compile-semantic-text-value 2)
    (operator semantic-text 1)
    (operator semantic-text-ref 1)
    (operator sir-recognizes-some 1)
  )
  (equations)
  (rewrites
    (rule compile-semantic-text-wrapper-definition
      (head
        (compile-semantic-text-definition
          ?label
          (rx-definition (semantic-text ?label) ?regex)))
      (body
        (compile-definition
          ?name
          (sir-definition ?name (sir-node ?label ?body)))
        (compile-semantic-text-grammar ?body ?regex)))

    (rule compile-semantic-text-reference-definition
      (head
        (compile-span-definition
          (semantic-text-ref ?name)
          (rx-definition (semantic-text-ref ?name) ?regex)))
      (body
        (compile-definition
          ?name (sir-definition ?name ?grammar))
        (compile-semantic-text-grammar ?grammar ?regex)))

    (rule compile-span-semantic-text-wrapper
      (head
        (compile-span-definition
          (semantic-text ?label)
          (rx-definition (semantic-text ?label) ?regex)))
      (body
        (compile-semantic-text-definition
          ?label
          (rx-definition (semantic-text ?label) ?regex))))

    (rule compile-semantic-text-any
      (head (compile-semantic-text-grammar sir-any rx-any))
      (body))
    (rule compile-semantic-text-char
      (head
        (compile-semantic-text-grammar
          (sir-char ?codepoint) (rx-char ?codepoint)))
      (body))
    (rule compile-semantic-text-class
      (head
        (compile-semantic-text-grammar
          (sir-class ?class) (rx-class ?class)))
      (body))
    (rule compile-semantic-text-eps
      (head
        (compile-semantic-text-grammar
          (sir-eps ?value) ?regex))
      (body (compile-semantic-text-value ?value ?regex)))
    (rule compile-semantic-text-lit
      (head
        (compile-semantic-text-grammar
          (sir-lit ?characters) ?regex))
      (body (compile-semantic-text-lit ?characters ?regex)))
    (rule compile-semantic-text-ref
      (head
        (compile-semantic-text-grammar
          (sir-ref ?name) (rx-ref (semantic-text-ref ?name))))
      (body))
    (rule compile-semantic-text-alt
      (head
        (compile-semantic-text-grammar
          (sir-alt ?left ?right) (rx-alt ?a ?b)))
      (body
        (compile-semantic-text-grammar ?left ?a)
        (compile-semantic-text-grammar ?right ?b)))
    (rule compile-semantic-text-seq
      (head
        (compile-semantic-text-grammar
          (sir-seq ?left ?right) (rx-seq ?a ?b)))
      (body
        (compile-semantic-text-grammar ?left ?a)
        (compile-semantic-text-grammar ?right ?b)))
    (rule compile-semantic-text-left
      (head
        (compile-semantic-text-grammar
          (sir-left ?left ?right) ?regex))
      (body
        (compile-semantic-text-grammar ?left ?regex)
        (sir-recognizes-some ?right)))
    (rule compile-semantic-text-right
      (head
        (compile-semantic-text-grammar
          (sir-right ?left ?right) ?regex))
      (body
        (sir-recognizes-some ?left)
        (compile-semantic-text-grammar ?right ?regex)))
    (rule compile-semantic-text-star
      (head
        (compile-semantic-text-grammar
          (sir-star ?body) (rx-star ?regex)))
      (body (compile-semantic-text-grammar ?body ?regex)))
    (rule compile-semantic-text-plus
      (head
        (compile-semantic-text-grammar
          (sir-plus ?body)
          (rx-seq ?regex (rx-star ?regex))))
      (body (compile-semantic-text-grammar ?body ?regex)))
    (rule compile-semantic-text-until
      (head
        (compile-semantic-text-grammar
          (sir-until ?end ?item) (rx-star ?regex)))
      (body
        (sir-recognizes-some ?end)
        (compile-semantic-text-grammar ?item ?regex)))

    (rule compile-semantic-text-value-nil
      (head (compile-semantic-text-value nil rx-eps))
      (body))
    (rule compile-semantic-text-value-codepoint
      (head
        (compile-semantic-text-value
          (cp ?value) (rx-char (cp ?value))))
      (body))
    (rule compile-semantic-text-value-pair
      (head
        (compile-semantic-text-value
          (pair ?left ?right) (rx-seq ?a ?b)))
      (body
        (compile-semantic-text-value ?left ?a)
        (compile-semantic-text-value ?right ?b)))
    (rule compile-semantic-text-value-cons
      (head
        (compile-semantic-text-value
          (cons ?first ?more) (rx-seq ?a ?b)))
      (body
        (compile-semantic-text-value ?first ?a)
        (compile-semantic-text-value ?more ?b)))

    (rule compile-semantic-text-lit-empty
      (head (compile-semantic-text-lit nil rx-eps))
      (body))
    (rule compile-semantic-text-lit-cons
      (head
        (compile-semantic-text-lit
          (cons ?codepoint ?more)
          (rx-seq (rx-char ?codepoint) ?rest)))
      (body (compile-semantic-text-lit ?more ?rest)))

    (rule sir-recognizes-some-any
      (head (sir-recognizes-some sir-any))
      (body))
    (rule sir-recognizes-some-eof
      (head (sir-recognizes-some sir-eof))
      (body))
    (rule sir-recognizes-some-char
      (head (sir-recognizes-some (sir-char ?codepoint)))
      (body))
    (rule sir-recognizes-some-class
      (head (sir-recognizes-some (sir-class ?class)))
      (body (member ?class ?codepoint)))
    (rule sir-recognizes-some-eps
      (head (sir-recognizes-some (sir-eps ?value)))
      (body))
    (rule sir-recognizes-some-lit
      (head (sir-recognizes-some (sir-lit ?characters)))
      (body))
    (rule sir-recognizes-some-ref
      (head (sir-recognizes-some (sir-ref ?name)))
      (body
        (compile-definition
          ?name (sir-definition ?name ?grammar))
        (sir-recognizes-some ?grammar)))
    (rule sir-recognizes-some-alt-left
      (head (sir-recognizes-some (sir-alt ?left ?right)))
      (body (sir-recognizes-some ?left)))
    (rule sir-recognizes-some-alt-right
      (head (sir-recognizes-some (sir-alt ?left ?right)))
      (body (sir-recognizes-some ?right)))
    (rule sir-recognizes-some-seq
      (head (sir-recognizes-some (sir-seq ?left ?right)))
      (body
        (sir-recognizes-some ?left)
        (sir-recognizes-some ?right)))
    (rule sir-recognizes-some-left
      (head (sir-recognizes-some (sir-left ?left ?right)))
      (body
        (sir-recognizes-some ?left)
        (sir-recognizes-some ?right)))
    (rule sir-recognizes-some-right
      (head (sir-recognizes-some (sir-right ?left ?right)))
      (body
        (sir-recognizes-some ?left)
        (sir-recognizes-some ?right)))
    (rule sir-recognizes-some-node
      (head (sir-recognizes-some (sir-node ?label ?body)))
      (body (sir-recognizes-some ?body)))
    (rule sir-recognizes-some-star
      (head (sir-recognizes-some (sir-star ?body)))
      (body))
    (rule sir-recognizes-some-plus
      (head (sir-recognizes-some (sir-plus ?body)))
      (body (sir-recognizes-some ?body)))
    (rule sir-recognizes-some-opt
      (head (sir-recognizes-some (sir-opt ?body)))
      (body))
    (rule sir-recognizes-some-until
      (head (sir-recognizes-some (sir-until ?end ?item)))
      (body (sir-recognizes-some ?end)))
    (rule sir-recognizes-some-peek
      (head (sir-recognizes-some (sir-peek ?body)))
      (body (sir-recognizes-some ?body)))
  ))
