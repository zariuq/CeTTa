; Reifiable one-carrier syntax GSLT. Atom, integer, and string literals are
; primitive terms. The finite signature declares compound operators by arity.
; Parsing is the ordinary Horn relation (parse Grammar Input Value Rest).

(gslt-presentation-v1 SyntaxCoreV1
  (signature
    (operator cons 2)
    (operator cp 1)
    (operator eps 1)
    (operator char 1)
    (operator class 1)
    (operator lit 1)
    (operator alt 2)
    (operator seq 2)
    (operator left 2)
    (operator right 2)
    (operator node 2)
    (operator ref 1)
    (operator star 1)
    (operator plus 1)
    (operator opt 1)
    (operator until 2)
    (operator pair 2)
    (operator some 1)
    (operator parse 4)
    (operator member 2)
    (operator definition 2))

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
