; Optional source-location former for scannerless syntax presentations.
;
; The declarative parser retains the exact input and residual suffixes around
; the payload.  The ParserPack lowering residualizes the same boundary as a
; SourceSpanActionV1 action; replay materializes a SourceSpanNodeV1 with its
; half-open scalar interval from the matched forest symbol.  Languages opt in
; at individual grammar nodes.

(gslt-presentation-v1 SourceSpanNodeV1
  (signature
    (operator span-node 2)
    (operator source-span-node 4)
    (operator sir-span-node 2)
    (operator parse 4)
    (operator compile-grammar 2)
    (operator collect-sir-class-use 2)
    (operator flatten-sir-production 3)
    (operator pp-production 4)
    (operator pp-label 2)
    (operator pp-items-cons 2)
    (operator pp-items-nil 0)
    (operator pp-nonterminal 1)
    (operator pp-sub 2)
    (operator pa-apply 2)
    (operator pa-cons 2)
    (operator pa-const 1)
    (operator pa-slot 1)
    (operator pa-nil 0)
    (operator q-zero 0))
  (equations)
  (rewrites
    (rule parse-span-node
      (head
        (parse
          (span-node ?label ?grammar)
          ?input
          (source-span-node ?label ?input ?rest ?value)
          ?rest))
      (body
        (parse ?grammar ?input ?value ?rest)))

    (rule compile-span-node
      (head
        (compile-grammar
          (span-node ?label ?grammar)
          (sir-span-node ?label ?body)))
      (body
        (compile-grammar ?grammar ?body)))

    (rule collect-sir-class-use-span-node
      (head
        (collect-sir-class-use
          (sir-span-node ?label ?body) ?class))
      (body
        (collect-sir-class-use ?body ?class)))

    (rule flatten-sir-span-node-parent
      (head
        (flatten-sir-production
          ?state (sir-span-node ?label ?body)
          (pp-production
            (pp-label ?state span-node)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state body))
              pp-items-nil)
            (pa-apply SourceSpanActionV1
              (pa-cons (pa-const ?label)
                (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))

    (rule flatten-sir-span-node-descend
      (head
        (flatten-sir-production
          ?state (sir-span-node ?label ?body) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state body) ?body ?production)))))
