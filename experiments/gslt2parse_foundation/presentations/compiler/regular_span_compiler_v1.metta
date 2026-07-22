; Recognition-preserving lowering from syntax SIR to a regular expression and
; then to a Thompson NFA. NFA state names are structural paths, so compilation
; does not depend on a host allocator or an authored state numbering.

(gslt-presentation-v1 RegularSpanCompilerV1
  (signature
    (operator compile-span-definition 2)
    (operator compile-span-grammar 2)
    (operator compile-span-lit 2)
    (operator compile-span-nfa 2)
    (operator span-nfa-edge 4)
    (operator rx-alt 2)
    (operator rx-char 1)
    (operator rx-class 1)
    (operator rx-definition 2)
    (operator rx-ref 1)
    (operator rx-seq 2)
    (operator rx-star 1)
    (operator rn-in 1)
    (operator rn-out 1)
    (operator rn-reference 2)
    (operator rn-root 1)
    (operator rn-sub 2)
    (operator nfa-accept 2)
    (operator nfa-any 2)
    (operator nfa-char 3)
    (operator nfa-class 3)
    (operator nfa-eof 2)
    (operator nfa-epsilon 2)
    (operator nfa-start 1)
  )
  (equations)
  (rewrites
    (rule compile-span-definition
      (head
        (compile-span-definition
          ?name (rx-definition ?name ?regex)))
      (body
        (compile-definition
          ?name (sir-definition ?name ?grammar))
        (compile-span-grammar ?grammar ?regex)))

    (rule compile-span-any
      (head (compile-span-grammar sir-any rx-any))
      (body))
    (rule compile-span-eof
      (head (compile-span-grammar sir-eof rx-eof))
      (body))
    (rule compile-span-char
      (head
        (compile-span-grammar
          (sir-char ?codepoint) (rx-char ?codepoint)))
      (body))
    (rule compile-span-class
      (head
        (compile-span-grammar
          (sir-class ?class) (rx-class ?class)))
      (body))
    (rule compile-span-eps
      (head (compile-span-grammar (sir-eps ?value) rx-eps))
      (body))
    (rule compile-span-lit
      (head
        (compile-span-grammar
          (sir-lit ?characters) ?regex))
      (body (compile-span-lit ?characters ?regex)))
    (rule compile-span-ref
      (head
        (compile-span-grammar
          (sir-ref ?name) (rx-ref ?name)))
      (body))
    (rule compile-span-alt
      (head
        (compile-span-grammar
          (sir-alt ?left ?right) (rx-alt ?a ?b)))
      (body
        (compile-span-grammar ?left ?a)
        (compile-span-grammar ?right ?b)))
    (rule compile-span-seq
      (head
        (compile-span-grammar
          (sir-seq ?left ?right) (rx-seq ?a ?b)))
      (body
        (compile-span-grammar ?left ?a)
        (compile-span-grammar ?right ?b)))
    (rule compile-span-left
      (head
        (compile-span-grammar
          (sir-left ?left ?right) (rx-seq ?a ?b)))
      (body
        (compile-span-grammar ?left ?a)
        (compile-span-grammar ?right ?b)))
    (rule compile-span-right
      (head
        (compile-span-grammar
          (sir-right ?left ?right) (rx-seq ?a ?b)))
      (body
        (compile-span-grammar ?left ?a)
        (compile-span-grammar ?right ?b)))
    (rule compile-span-node
      (head
        (compile-span-grammar
          (sir-node ?label ?body) ?regex))
      (body (compile-span-grammar ?body ?regex)))
    (rule compile-span-star
      (head
        (compile-span-grammar
          (sir-star ?body) (rx-star ?regex)))
      (body (compile-span-grammar ?body ?regex)))
    (rule compile-span-plus
      (head
        (compile-span-grammar
          (sir-plus ?body) (rx-seq ?regex (rx-star ?regex))))
      (body (compile-span-grammar ?body ?regex)))
    (rule compile-span-opt
      (head
        (compile-span-grammar
          (sir-opt ?body) (rx-alt rx-eps ?regex)))
      (body (compile-span-grammar ?body ?regex)))
    (rule compile-span-until
      (head
        (compile-span-grammar
          (sir-until ?end ?item)
          (rx-seq (rx-star ?item-regex) ?end-regex)))
      (body
        (compile-span-grammar ?item ?item-regex)
        (compile-span-grammar ?end ?end-regex)))

    (rule compile-span-lit-empty
      (head (compile-span-lit nil rx-eps))
      (body))
    (rule compile-span-lit-cons
      (head
        (compile-span-lit
          (cons ?codepoint ?more)
          (rx-seq (rx-char ?codepoint) ?rest)))
      (body (compile-span-lit ?more ?rest)))

    (rule compile-span-nfa-start
      (head
        (compile-span-nfa
          ?tag (nfa-start (rn-in (rn-root ?tag)))))
      (body
        (compile-span-definition
          ?tag (rx-definition ?tag ?regex))))
    (rule compile-span-nfa-accept
      (head
        (compile-span-nfa
          ?tag (nfa-accept (rn-out (rn-root ?tag)) ?tag)))
      (body
        (compile-span-definition
          ?tag (rx-definition ?tag ?regex))))
    (rule compile-span-nfa-edge
      (head (compile-span-nfa ?tag ?edge))
      (body
        (compile-span-definition
          ?tag (rx-definition ?tag ?regex))
        (span-nfa-edge ?tag ?regex (rn-root ?tag) ?edge)))

    (rule span-nfa-eps
      (head
        (span-nfa-edge
          ?tag rx-eps ?path
          (nfa-epsilon (rn-in ?path) (rn-out ?path))))
      (body))
    (rule span-nfa-char
      (head
        (span-nfa-edge
          ?tag (rx-char ?codepoint) ?path
          (nfa-char (rn-in ?path) ?codepoint (rn-out ?path))))
      (body))
    (rule span-nfa-class
      (head
        (span-nfa-edge
          ?tag (rx-class ?class) ?path
          (nfa-class (rn-in ?path) ?class (rn-out ?path))))
      (body))
    (rule span-nfa-any
      (head
        (span-nfa-edge
          ?tag rx-any ?path
          (nfa-any (rn-in ?path) (rn-out ?path))))
      (body))
    (rule span-nfa-eof
      (head
        (span-nfa-edge
          ?tag rx-eof ?path
          (nfa-eof (rn-in ?path) (rn-out ?path))))
      (body))

    (rule span-nfa-alt-enter-left
      (head
        (span-nfa-edge
          ?tag (rx-alt ?left ?right) ?path
          (nfa-epsilon
            (rn-in ?path) (rn-in (rn-sub ?path left)))))
      (body))
    (rule span-nfa-alt-enter-right
      (head
        (span-nfa-edge
          ?tag (rx-alt ?left ?right) ?path
          (nfa-epsilon
            (rn-in ?path) (rn-in (rn-sub ?path right)))))
      (body))
    (rule span-nfa-alt-descend-left
      (head
        (span-nfa-edge
          ?tag (rx-alt ?left ?right) ?path ?edge))
      (body
        (span-nfa-edge
          ?tag ?left (rn-sub ?path left) ?edge)))
    (rule span-nfa-alt-descend-right
      (head
        (span-nfa-edge
          ?tag (rx-alt ?left ?right) ?path ?edge))
      (body
        (span-nfa-edge
          ?tag ?right (rn-sub ?path right) ?edge)))
    (rule span-nfa-alt-leave-left
      (head
        (span-nfa-edge
          ?tag (rx-alt ?left ?right) ?path
          (nfa-epsilon
            (rn-out (rn-sub ?path left)) (rn-out ?path))))
      (body))
    (rule span-nfa-alt-leave-right
      (head
        (span-nfa-edge
          ?tag (rx-alt ?left ?right) ?path
          (nfa-epsilon
            (rn-out (rn-sub ?path right)) (rn-out ?path))))
      (body))

    (rule span-nfa-seq-enter
      (head
        (span-nfa-edge
          ?tag (rx-seq ?left ?right) ?path
          (nfa-epsilon
            (rn-in ?path) (rn-in (rn-sub ?path left)))))
      (body))
    (rule span-nfa-seq-descend-left
      (head
        (span-nfa-edge
          ?tag (rx-seq ?left ?right) ?path ?edge))
      (body
        (span-nfa-edge
          ?tag ?left (rn-sub ?path left) ?edge)))
    (rule span-nfa-seq-join
      (head
        (span-nfa-edge
          ?tag (rx-seq ?left ?right) ?path
          (nfa-epsilon
            (rn-out (rn-sub ?path left))
            (rn-in (rn-sub ?path right)))))
      (body))
    (rule span-nfa-seq-descend-right
      (head
        (span-nfa-edge
          ?tag (rx-seq ?left ?right) ?path ?edge))
      (body
        (span-nfa-edge
          ?tag ?right (rn-sub ?path right) ?edge)))
    (rule span-nfa-seq-leave
      (head
        (span-nfa-edge
          ?tag (rx-seq ?left ?right) ?path
          (nfa-epsilon
            (rn-out (rn-sub ?path right)) (rn-out ?path))))
      (body))

    (rule span-nfa-star-empty
      (head
        (span-nfa-edge
          ?tag (rx-star ?body) ?path
          (nfa-epsilon (rn-in ?path) (rn-out ?path))))
      (body))
    (rule span-nfa-star-enter
      (head
        (span-nfa-edge
          ?tag (rx-star ?body) ?path
          (nfa-epsilon
            (rn-in ?path) (rn-in (rn-sub ?path body)))))
      (body))
    (rule span-nfa-star-descend
      (head
        (span-nfa-edge
          ?tag (rx-star ?body) ?path ?edge))
      (body
        (span-nfa-edge
          ?tag ?body (rn-sub ?path body) ?edge)))
    (rule span-nfa-star-repeat
      (head
        (span-nfa-edge
          ?tag (rx-star ?body) ?path
          (nfa-epsilon
            (rn-out (rn-sub ?path body))
            (rn-in (rn-sub ?path body)))))
      (body))
    (rule span-nfa-star-leave
      (head
        (span-nfa-edge
          ?tag (rx-star ?body) ?path
          (nfa-epsilon
            (rn-out (rn-sub ?path body)) (rn-out ?path))))
      (body))

    (rule span-nfa-ref-enter
      (head
        (span-nfa-edge
          ?tag (rx-ref ?name) ?path
          (nfa-epsilon
            (rn-in ?path)
            (rn-in (rn-reference ?path ?name)))))
      (body))
    (rule span-nfa-ref-descend
      (head
        (span-nfa-edge
          ?tag (rx-ref ?name) ?path ?edge))
      (body
        (compile-span-definition
          ?name (rx-definition ?name ?regex))
        (span-nfa-edge
          ?tag ?regex (rn-reference ?path ?name) ?edge)))
    (rule span-nfa-ref-leave
      (head
        (span-nfa-edge
          ?tag (rx-ref ?name) ?path
          (nfa-epsilon
            (rn-out (rn-reference ?path ?name))
            (rn-out ?path))))
      (body))
  ))
