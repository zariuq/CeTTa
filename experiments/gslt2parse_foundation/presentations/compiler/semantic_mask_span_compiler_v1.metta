; Lower one text-valued syntax node to an annotated regular transducer.
; Consuming edges say whether their source scalar is retained verbatim,
; discarded, or retained as the payload of a named projection wrapper.
; References are namespaced by semantic mode, so definitions remain shared
; without confusing the value retained at two different call sites.
;
; This is a deliberately finite, fail-closed fragment.  A nested node inside
; an already active wrapper and semantic values emitted without consuming a
; scalar have no lowering rule.  Trailing positive guards are admitted only
; through the separately certified guarded-prefix projection; arbitrary peek
; placement remains outside this compiler.  Such grammars require a richer
; transducer.

(gslt-presentation-v1 SemanticMaskSpanCompilerV1
  (signature
    (operator compile-semantic-mask-definition 2)
    (operator compile-semantic-mask-grammar 3)
    (operator compile-semantic-mask-lit 3)
    (operator compile-semantic-mask-nfa 2)
    (operator compile-semantic-mask-root-link 2)
    (operator semantic-mask-nfa-edge 4)
    (operator semantic-mask-wrapper 1)
    (operator semantic-mask-ref 2)
    (operator mrx-alt 2)
    (operator mrx-any 1)
    (operator mrx-char 2)
    (operator mrx-class 2)
    (operator mrx-definition 2)
    (operator mrx-ref 1)
    (operator mrx-seq 2)
    (operator mrx-star 1)
    (operator smn-in 1)
    (operator smn-out 1)
    (operator smn-reference 2)
    (operator smn-root 1)
    (operator smn-sub 2)
    (operator semantic-mask-nfa-accept 2)
    (operator semantic-mask-nfa-any 3)
    (operator semantic-mask-nfa-char 4)
    (operator semantic-mask-nfa-class 4)
    (operator semantic-mask-nfa-epsilon 2)
    (operator semantic-mask-nfa-eof 2)
    (operator semantic-mask-nfa-start 1)
  )
  (equations)
  (rewrites
    (rule compile-semantic-mask-root-definition
      (head
        (compile-semantic-mask-definition
          ?label (mrx-definition ?label ?regex)))
      (body
        (compile-definition
          ?name (sir-definition ?name (sir-node ?label ?body)))
        (compile-semantic-mask-grammar
          semantic-mask-copy ?body ?regex)))

    (rule compile-semantic-mask-left-node-root-definition
      (head
        (compile-semantic-mask-definition
          ?label
          (mrx-definition ?label (mrx-seq ?node-regex ?suffix-regex))))
      (body
        (compile-definition
          ?name
          (sir-definition
            ?name (sir-left (sir-node ?label ?body) ?suffix)))
        (compile-semantic-mask-grammar
          semantic-mask-copy (sir-node ?label ?body) ?node-regex)
        (compile-semantic-mask-grammar
          semantic-mask-drop ?suffix ?suffix-regex)))

    (rule compile-semantic-mask-guarded-root-definition
      (head
        (compile-semantic-mask-definition
          ?label (mrx-definition ?label ?regex)))
      (body
        (compile-definition
          ?name
          (sir-definition
            ?name
            (sir-node
              ?label
              (sir-left ?prefix (sir-peek ?guard)))))
        (compile-semantic-mask-grammar
          semantic-mask-copy ?prefix ?regex)))

    (rule compile-semantic-mask-root-link
      (head (compile-semantic-mask-root-link ?name ?label))
      (body
        (compile-definition
          ?name (sir-definition ?name (sir-node ?label ?body)))
        (compile-semantic-mask-grammar
          semantic-mask-copy ?body ?regex)))

    (rule compile-semantic-mask-left-node-root-link
      (head (compile-semantic-mask-root-link ?name ?label))
      (body
        (compile-definition
          ?name
          (sir-definition
            ?name (sir-left (sir-node ?label ?body) ?suffix)))
        (compile-semantic-mask-grammar
          semantic-mask-copy (sir-node ?label ?body) ?node-regex)
        (compile-semantic-mask-grammar
          semantic-mask-drop ?suffix ?suffix-regex)))

    (rule compile-semantic-mask-guarded-root-link
      (head (compile-semantic-mask-root-link ?name ?label))
      (body
        (compile-definition
          ?name
          (sir-definition
            ?name
            (sir-node
              ?label
              (sir-left ?prefix (sir-peek ?guard)))))
        (compile-semantic-mask-grammar
          semantic-mask-copy ?prefix ?regex)))

    (rule compile-semantic-mask-reference-definition
      (head
        (compile-semantic-mask-definition
          (semantic-mask-ref ?mode ?name)
          (mrx-definition (semantic-mask-ref ?mode ?name) ?regex)))
      (body
        (compile-definition
          ?name (sir-definition ?name ?grammar))
        (compile-semantic-mask-grammar ?mode ?grammar ?regex)))

    (rule compile-semantic-mask-any
      (head
        (compile-semantic-mask-grammar
          ?mode sir-any (mrx-any ?mode)))
      (body))
    (rule compile-semantic-mask-char
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-char ?codepoint) (mrx-char ?codepoint ?mode)))
      (body))
    (rule compile-semantic-mask-class
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-class ?class) (mrx-class ?class ?mode)))
      (body))
    (rule compile-semantic-mask-eps-drop
      (head
        (compile-semantic-mask-grammar
          semantic-mask-drop (sir-eps ?value) mrx-eps))
      (body))
    (rule compile-semantic-mask-eps-nil
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-eps nil) mrx-eps))
      (body))
    (rule compile-semantic-mask-eof
      (head
        (compile-semantic-mask-grammar
          ?mode sir-eof mrx-eof))
      (body))
    (rule compile-semantic-mask-lit
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-lit ?characters) ?regex))
      (body
        (compile-semantic-mask-lit ?mode ?characters ?regex)))
    (rule compile-semantic-mask-ref
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-ref ?name)
          (mrx-ref (semantic-mask-ref ?mode ?name))))
      (body))
    (rule compile-semantic-mask-alt
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-alt ?left ?right) (mrx-alt ?a ?b)))
      (body
        (compile-semantic-mask-grammar ?mode ?left ?a)
        (compile-semantic-mask-grammar ?mode ?right ?b)))
    (rule compile-semantic-mask-seq
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-seq ?left ?right) (mrx-seq ?a ?b)))
      (body
        (compile-semantic-mask-grammar ?mode ?left ?a)
        (compile-semantic-mask-grammar ?mode ?right ?b)))
    (rule compile-semantic-mask-left
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-left ?left ?right) (mrx-seq ?a ?b)))
      (body
        (compile-semantic-mask-grammar ?mode ?left ?a)
        (compile-semantic-mask-grammar semantic-mask-drop ?right ?b)))
    (rule compile-semantic-mask-right
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-right ?left ?right) (mrx-seq ?a ?b)))
      (body
        (compile-semantic-mask-grammar semantic-mask-drop ?left ?a)
        (compile-semantic-mask-grammar ?mode ?right ?b)))
    (rule compile-semantic-mask-node-copy
      (head
        (compile-semantic-mask-grammar
          semantic-mask-copy (sir-node ?label ?body) ?regex))
      (body
        (compile-semantic-mask-grammar
          (semantic-mask-wrapper ?label) ?body ?regex)))
    (rule compile-semantic-mask-node-drop
      (head
        (compile-semantic-mask-grammar
          semantic-mask-drop (sir-node ?label ?body) ?regex))
      (body
        (compile-semantic-mask-grammar
          semantic-mask-drop ?body ?regex)))
    (rule compile-semantic-mask-star
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-star ?body) (mrx-star ?regex)))
      (body
        (compile-semantic-mask-grammar ?mode ?body ?regex)))
    (rule compile-semantic-mask-plus
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-plus ?body)
          (mrx-seq ?regex (mrx-star ?regex))))
      (body
        (compile-semantic-mask-grammar ?mode ?body ?regex)))
    (rule compile-semantic-mask-opt
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-opt ?body) (mrx-alt mrx-eps ?regex)))
      (body
        (compile-semantic-mask-grammar ?mode ?body ?regex)))
    (rule compile-semantic-mask-until
      (head
        (compile-semantic-mask-grammar
          ?mode (sir-until ?end ?item)
          (mrx-seq (mrx-star ?item-regex) ?end-regex)))
      (body
        (compile-semantic-mask-grammar ?mode ?item ?item-regex)
        (compile-semantic-mask-grammar
          semantic-mask-drop ?end ?end-regex)))

    (rule compile-semantic-mask-lit-empty
      (head
        (compile-semantic-mask-lit ?mode nil mrx-eps))
      (body))
    (rule compile-semantic-mask-lit-cons
      (head
        (compile-semantic-mask-lit
          ?mode (cons ?codepoint ?more)
          (mrx-seq (mrx-char ?codepoint ?mode) ?rest)))
      (body
        (compile-semantic-mask-lit ?mode ?more ?rest)))

    (rule compile-semantic-mask-nfa-start
      (head
        (compile-semantic-mask-nfa
          ?tag (semantic-mask-nfa-start (smn-in (smn-root ?tag)))))
      (body
        (compile-semantic-mask-definition
          ?tag (mrx-definition ?tag ?regex))))
    (rule compile-semantic-mask-nfa-accept
      (head
        (compile-semantic-mask-nfa
          ?tag
          (semantic-mask-nfa-accept
            (smn-out (smn-root ?tag)) ?tag)))
      (body
        (compile-semantic-mask-definition
          ?tag (mrx-definition ?tag ?regex))))
    (rule compile-semantic-mask-nfa-edge
      (head
        (compile-semantic-mask-nfa ?tag ?edge))
      (body
        (compile-semantic-mask-definition
          ?tag (mrx-definition ?tag ?regex))
        (semantic-mask-nfa-edge
          ?tag ?regex (smn-root ?tag) ?edge)))

    (rule semantic-mask-nfa-eps
      (head
        (semantic-mask-nfa-edge
          ?tag mrx-eps ?path
          (semantic-mask-nfa-epsilon
            (smn-in ?path) (smn-out ?path))))
      (body))
    (rule semantic-mask-nfa-eof
      (head
        (semantic-mask-nfa-edge
          ?tag mrx-eof ?path
          (semantic-mask-nfa-eof
            (smn-in ?path) (smn-out ?path))))
      (body))
    (rule semantic-mask-nfa-char
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-char ?codepoint ?action) ?path
          (semantic-mask-nfa-char
            (smn-in ?path) ?codepoint (smn-out ?path) ?action)))
      (body))
    (rule semantic-mask-nfa-class
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-class ?class ?action) ?path
          (semantic-mask-nfa-class
            (smn-in ?path) ?class (smn-out ?path) ?action)))
      (body))
    (rule semantic-mask-nfa-any
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-any ?action) ?path
          (semantic-mask-nfa-any
            (smn-in ?path) (smn-out ?path) ?action)))
      (body))

    (rule semantic-mask-nfa-alt-enter-left
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-alt ?left ?right) ?path
          (semantic-mask-nfa-epsilon
            (smn-in ?path) (smn-in (smn-sub ?path left)))))
      (body))
    (rule semantic-mask-nfa-alt-enter-right
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-alt ?left ?right) ?path
          (semantic-mask-nfa-epsilon
            (smn-in ?path) (smn-in (smn-sub ?path right)))))
      (body))
    (rule semantic-mask-nfa-alt-descend-left
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-alt ?left ?right) ?path ?edge))
      (body
        (semantic-mask-nfa-edge
          ?tag ?left (smn-sub ?path left) ?edge)))
    (rule semantic-mask-nfa-alt-descend-right
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-alt ?left ?right) ?path ?edge))
      (body
        (semantic-mask-nfa-edge
          ?tag ?right (smn-sub ?path right) ?edge)))
    (rule semantic-mask-nfa-alt-leave-left
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-alt ?left ?right) ?path
          (semantic-mask-nfa-epsilon
            (smn-out (smn-sub ?path left)) (smn-out ?path))))
      (body))
    (rule semantic-mask-nfa-alt-leave-right
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-alt ?left ?right) ?path
          (semantic-mask-nfa-epsilon
            (smn-out (smn-sub ?path right)) (smn-out ?path))))
      (body))

    (rule semantic-mask-nfa-seq-enter
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-seq ?left ?right) ?path
          (semantic-mask-nfa-epsilon
            (smn-in ?path) (smn-in (smn-sub ?path left)))))
      (body))
    (rule semantic-mask-nfa-seq-descend-left
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-seq ?left ?right) ?path ?edge))
      (body
        (semantic-mask-nfa-edge
          ?tag ?left (smn-sub ?path left) ?edge)))
    (rule semantic-mask-nfa-seq-join
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-seq ?left ?right) ?path
          (semantic-mask-nfa-epsilon
            (smn-out (smn-sub ?path left))
            (smn-in (smn-sub ?path right)))))
      (body))
    (rule semantic-mask-nfa-seq-descend-right
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-seq ?left ?right) ?path ?edge))
      (body
        (semantic-mask-nfa-edge
          ?tag ?right (smn-sub ?path right) ?edge)))
    (rule semantic-mask-nfa-seq-leave
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-seq ?left ?right) ?path
          (semantic-mask-nfa-epsilon
            (smn-out (smn-sub ?path right)) (smn-out ?path))))
      (body))

    (rule semantic-mask-nfa-star-empty
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-star ?body) ?path
          (semantic-mask-nfa-epsilon
            (smn-in ?path) (smn-out ?path))))
      (body))
    (rule semantic-mask-nfa-star-enter
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-star ?body) ?path
          (semantic-mask-nfa-epsilon
            (smn-in ?path) (smn-in (smn-sub ?path body)))))
      (body))
    (rule semantic-mask-nfa-star-descend
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-star ?body) ?path ?edge))
      (body
        (semantic-mask-nfa-edge
          ?tag ?body (smn-sub ?path body) ?edge)))
    (rule semantic-mask-nfa-star-repeat
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-star ?body) ?path
          (semantic-mask-nfa-epsilon
            (smn-out (smn-sub ?path body))
            (smn-in (smn-sub ?path body)))))
      (body))
    (rule semantic-mask-nfa-star-leave
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-star ?body) ?path
          (semantic-mask-nfa-epsilon
            (smn-out (smn-sub ?path body)) (smn-out ?path))))
      (body))

    (rule semantic-mask-nfa-ref-enter
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-ref ?name) ?path
          (semantic-mask-nfa-epsilon
            (smn-in ?path)
            (smn-in (smn-reference ?path ?name)))))
      (body))
    (rule semantic-mask-nfa-ref-descend
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-ref ?name) ?path ?edge))
      (body
        (compile-semantic-mask-definition
          ?name (mrx-definition ?name ?regex))
        (semantic-mask-nfa-edge
          ?tag ?regex (smn-reference ?path ?name) ?edge)))
    (rule semantic-mask-nfa-ref-leave
      (head
        (semantic-mask-nfa-edge
          ?tag (mrx-ref ?name) ?path
          (semantic-mask-nfa-epsilon
            (smn-out (smn-reference ?path ?name))
            (smn-out ?path))))
      (body))
  ))
