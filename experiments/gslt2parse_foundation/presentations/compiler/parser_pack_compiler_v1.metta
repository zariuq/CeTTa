(gslt-presentation-v1 ParserPackCompilerV1
  (signature
    (operator compile-pack-class-clause 2)
    (operator compile-pack-exclusions 3)
    (operator compile-pack-production 2)
    (operator flatten-sir-production 3)
    (operator pack-action 3)
    (operator pack-action-list 3)
    (operator pack-binding 2)
    (operator pack-bindings-cons 2)
    (operator pack-lit 4)
    (operator pack-lookup 3)
    (operator pack-rel-call 2)
    (operator pack-rel-items 5)
  )
  (equations)
  (rewrites
    (rule compile-pack-class-point
      (head
        (compile-pack-class-clause
          ?rule
          (pp-class-point ?class (cp ?value))))
      (body
        (source-rule
          ?presentation
          (q-rule
            (q-sym ?rule)
            (q-app
              (q-sym member)
              (q-cons
                (q-sym ?class)
                (q-cons
                  (q-app
                    (q-sym cp)
                    (q-cons (q-int ?value) q-nil))
                  q-nil)))
            q-nil))))

    (rule compile-pack-class-except
      (head
        (compile-pack-class-clause
          ?rule
          (pp-class-except ?class ?exclusions)))
      (body
        (source-rule
          ?presentation
          (q-rule
            (q-sym ?rule)
            (q-app
              (q-sym member)
              (q-cons
                (q-sym ?class)
                (q-cons (q-var ?index) q-nil)))
            ?body))
        (compile-pack-exclusions ?index ?body ?exclusions)))

    (rule compile-pack-exclusions-empty
      (head
        (compile-pack-exclusions
          ?index q-nil pp-points-nil))
      (body))
    (rule compile-pack-exclusions-left
      (head
        (compile-pack-exclusions
          ?index
          (q-cons
            (q-app
              (q-sym different)
              (q-cons
                (q-var ?index)
                (q-cons
                  (q-app
                    (q-sym cp)
                    (q-cons (q-int ?value) q-nil))
                  q-nil)))
            ?body)
          (pp-points-cons (cp ?value) ?exclusions)))
      (body
        (compile-pack-exclusions ?index ?body ?exclusions)))
    (rule compile-pack-exclusions-right
      (head
        (compile-pack-exclusions
          ?index
          (q-cons
            (q-app
              (q-sym different)
              (q-cons
                (q-app
                  (q-sym cp)
                  (q-cons (q-int ?value) q-nil))
                (q-cons (q-var ?index) q-nil)))
            ?body)
          (pp-points-cons (cp ?value) ?exclusions)))
      (body
        (compile-pack-exclusions ?index ?body ?exclusions)))

    (rule compile-pack-definition
      (head (compile-pack-production ?name ?production))
      (body
        (compile-definition
          ?name (sir-definition ?name ?grammar))
        (flatten-sir-production
          (pp-def ?name) ?grammar ?production)))

    (rule compile-pack-relational-production
      (head
        (compile-pack-production
          ?rule
          (pp-production
            (pp-label (pp-rel ?state) ?rule)
            (pp-rel ?state)
            ?packed-items
            ?packed-action)))
      (body
        (compile-cfg-production
          ?quoted-rule
          (sir-source-production
            ?presentation
            (sir-production ?rule ?state ?items ?action)))
        (pack-rel-items
          ?items q-zero ?bindings ?packed-items ?next-slot)
        (pack-action ?action ?bindings ?packed-action)))

    (rule flatten-sir-any
      (head
        (flatten-sir-production
          ?state sir-any
          (pp-production
            (pp-label ?state any)
            ?state
            (pp-items-cons (pp-terminal pp-terminal-any) pp-items-nil)
            (pa-slot q-zero))))
      (body))
    (rule flatten-sir-eof
      (head
        (flatten-sir-production
          ?state sir-eof
          (pp-production
            (pp-label ?state eof)
            ?state
            (pp-items-cons (pp-terminal pp-terminal-eof) pp-items-nil)
            (pa-const eof))))
      (body))
    (rule flatten-sir-char
      (head
        (flatten-sir-production
          ?state (sir-char ?codepoint)
          (pp-production
            (pp-label ?state char)
            ?state
            (pp-items-cons
              (pp-terminal (pp-terminal-char ?codepoint))
              pp-items-nil)
            (pa-slot q-zero))))
      (body))
    (rule flatten-sir-class
      (head
        (flatten-sir-production
          ?state (sir-class ?class)
          (pp-production
            (pp-label ?state class)
            ?state
            (pp-items-cons
              (pp-terminal (pp-terminal-class ?class))
              pp-items-nil)
            (pa-slot q-zero))))
      (body))
    (rule flatten-sir-eps
      (head
        (flatten-sir-production
          ?state (sir-eps ?value)
          (pp-production
            (pp-label ?state eps)
            ?state
            pp-items-nil
            (pa-const ?value))))
      (body))
    (rule flatten-sir-lit
      (head
        (flatten-sir-production
          ?state (sir-lit ?characters)
          (pp-production
            (pp-label ?state lit)
            ?state ?items ?action)))
      (body
        (pack-lit ?characters q-zero ?items ?action)))
    (rule flatten-sir-ref
      (head
        (flatten-sir-production
          ?state (sir-ref ?name)
          (pp-production
            (pp-label ?state ref)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-def ?name))
              pp-items-nil)
            (pa-slot q-zero))))
      (body))
    (rule flatten-sir-rel
      (head
        (flatten-sir-production
          ?state (sir-rel ?name)
          (pp-production
            (pp-label ?state rel)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-rel ?name))
              pp-items-nil)
            (pa-slot q-zero))))
      (body))

    (rule flatten-sir-alt-left
      (head
        (flatten-sir-production
          ?state (sir-alt ?left ?right)
          (pp-production
            (pp-label ?state alt-left)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state left))
              pp-items-nil)
            (pa-slot q-zero))))
      (body))
    (rule flatten-sir-alt-right
      (head
        (flatten-sir-production
          ?state (sir-alt ?left ?right)
          (pp-production
            (pp-label ?state alt-right)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state right))
              pp-items-nil)
            (pa-slot q-zero))))
      (body))
    (rule flatten-sir-alt-descend-left
      (head
        (flatten-sir-production
          ?state (sir-alt ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state left) ?left ?production)))
    (rule flatten-sir-alt-descend-right
      (head
        (flatten-sir-production
          ?state (sir-alt ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state right) ?right ?production)))

    (rule flatten-sir-seq-parent
      (head
        (flatten-sir-production
          ?state (sir-seq ?left ?right)
          (pp-production
            (pp-label ?state seq)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state left))
              (pp-items-cons
                (pp-nonterminal (pp-sub ?state right))
                pp-items-nil))
            (pa-apply pair
              (pa-cons (pa-slot q-zero)
                (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule flatten-sir-seq-descend-left
      (head
        (flatten-sir-production
          ?state (sir-seq ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state left) ?left ?production)))
    (rule flatten-sir-seq-descend-right
      (head
        (flatten-sir-production
          ?state (sir-seq ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state right) ?right ?production)))

    (rule flatten-sir-left-parent
      (head
        (flatten-sir-production
          ?state (sir-left ?left ?right)
          (pp-production
            (pp-label ?state left)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state left))
              (pp-items-cons
                (pp-nonterminal (pp-sub ?state right))
                pp-items-nil))
            (pa-slot q-zero))))
      (body))
    (rule flatten-sir-left-descend-left
      (head
        (flatten-sir-production
          ?state (sir-left ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state left) ?left ?production)))
    (rule flatten-sir-left-descend-right
      (head
        (flatten-sir-production
          ?state (sir-left ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state right) ?right ?production)))

    (rule flatten-sir-right-parent
      (head
        (flatten-sir-production
          ?state (sir-right ?left ?right)
          (pp-production
            (pp-label ?state right)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state left))
              (pp-items-cons
                (pp-nonterminal (pp-sub ?state right))
                pp-items-nil))
            (pa-slot (q-succ q-zero)))))
      (body))
    (rule flatten-sir-right-descend-left
      (head
        (flatten-sir-production
          ?state (sir-right ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state left) ?left ?production)))
    (rule flatten-sir-right-descend-right
      (head
        (flatten-sir-production
          ?state (sir-right ?left ?right) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state right) ?right ?production)))

    (rule flatten-sir-node-parent
      (head
        (flatten-sir-production
          ?state (sir-node ?label ?body)
          (pp-production
            (pp-label ?state node)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state body))
              pp-items-nil)
            (pa-apply node
              (pa-cons (pa-const ?label)
                (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule flatten-sir-node-descend
      (head
        (flatten-sir-production
          ?state (sir-node ?label ?body) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state body) ?body ?production)))

    (rule flatten-sir-star-empty
      (head
        (flatten-sir-production
          ?state (sir-star ?body)
          (pp-production
            (pp-label ?state star-empty)
            ?state pp-items-nil (pa-const nil))))
      (body))
    (rule flatten-sir-star-cons
      (head
        (flatten-sir-production
          ?state (sir-star ?body)
          (pp-production
            (pp-label ?state star-cons)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state body))
              (pp-items-cons
                (pp-nonterminal ?state)
                pp-items-nil))
            (pa-apply cons
              (pa-cons (pa-slot q-zero)
                (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule flatten-sir-star-descend
      (head
        (flatten-sir-production
          ?state (sir-star ?body) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state body) ?body ?production)))

    (rule flatten-sir-plus-one
      (head
        (flatten-sir-production
          ?state (sir-plus ?body)
          (pp-production
            (pp-label ?state plus-one)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state body))
              pp-items-nil)
            (pa-apply cons
              (pa-cons (pa-slot q-zero)
                (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule flatten-sir-plus-more
      (head
        (flatten-sir-production
          ?state (sir-plus ?body)
          (pp-production
            (pp-label ?state plus-more)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state body))
              (pp-items-cons
                (pp-nonterminal ?state)
                pp-items-nil))
            (pa-apply cons
              (pa-cons (pa-slot q-zero)
                (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule flatten-sir-plus-descend
      (head
        (flatten-sir-production
          ?state (sir-plus ?body) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state body) ?body ?production)))

    (rule flatten-sir-opt-none
      (head
        (flatten-sir-production
          ?state (sir-opt ?body)
          (pp-production
            (pp-label ?state opt-none)
            ?state pp-items-nil (pa-const none))))
      (body))
    (rule flatten-sir-opt-some
      (head
        (flatten-sir-production
          ?state (sir-opt ?body)
          (pp-production
            (pp-label ?state opt-some)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state body))
              pp-items-nil)
            (pa-apply some
              (pa-cons (pa-slot q-zero) pa-nil)))))
      (body))
    (rule flatten-sir-opt-descend
      (head
        (flatten-sir-production
          ?state (sir-opt ?body) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state body) ?body ?production)))

    (rule flatten-sir-until-end
      (head
        (flatten-sir-production
          ?state (sir-until ?end ?item)
          (pp-production
            (pp-label ?state until-end)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state end))
              pp-items-nil)
            (pa-const nil))))
      (body))
    (rule flatten-sir-until-item
      (head
        (flatten-sir-production
          ?state (sir-until ?end ?item)
          (pp-production
            (pp-label ?state until-item)
            ?state
            (pp-items-cons
              (pp-nonterminal (pp-sub ?state item))
              (pp-items-cons
                (pp-nonterminal ?state)
                pp-items-nil))
            (pa-apply cons
              (pa-cons (pa-slot q-zero)
                (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule flatten-sir-until-descend-end
      (head
        (flatten-sir-production
          ?state (sir-until ?end ?item) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state end) ?end ?production)))
    (rule flatten-sir-until-descend-item
      (head
        (flatten-sir-production
          ?state (sir-until ?end ?item) ?production))
      (body
        (flatten-sir-production
          (pp-sub ?state item) ?item ?production)))

    (rule pack-lit-empty
      (head (pack-lit nil ?slot pp-items-nil (pa-const nil)))
      (body))
    (rule pack-lit-cons
      (head
        (pack-lit
          (cons ?codepoint ?characters)
          ?slot
          (pp-items-cons
            (pp-terminal (pp-terminal-char ?codepoint))
            ?items)
          (pa-apply cons
            (pa-cons (pa-slot ?slot)
              (pa-cons ?action pa-nil)))))
      (body
        (pack-lit
          ?characters (q-succ ?slot) ?items ?action)))

    (rule pack-rel-items-empty
      (head
        (pack-rel-items
          sir-items-nil ?slot pack-bindings-nil pp-items-nil ?slot))
      (body))
    (rule pack-rel-items-cons
      (head
        (pack-rel-items
          (sir-items-cons
            (sir-call ?grammar (q-var ?variable)) ?more)
          ?slot
          (pack-bindings-cons
            (pack-binding ?variable ?slot) ?bindings)
          (pp-items-cons ?packed-call ?packed-more)
          ?final-slot))
      (body
        (pack-rel-call ?grammar ?packed-call)
        (pack-rel-items
          ?more (q-succ ?slot) ?bindings ?packed-more ?final-slot)))

    (rule pack-rel-call-ref
      (head
        (pack-rel-call
          (q-app
            (q-sym ref)
            (q-cons (q-sym ?name) q-nil))
          (pp-nonterminal (pp-def ?name))))
      (body))
    (rule pack-rel-call-state
      (head
        (pack-rel-call
          (q-sym ?state)
          (pp-nonterminal (pp-rel ?state))))
      (body (compile-cfg-state ?state)))

    (rule pack-action-symbol
      (head
        (pack-action (q-sym ?name) ?bindings (pa-const ?name)))
      (body))
    (rule pack-action-integer
      (head
        (pack-action (q-int ?value) ?bindings (pa-const ?value)))
      (body))
    (rule pack-action-string
      (head
        (pack-action (q-str ?value) ?bindings (pa-const ?value)))
      (body))
    (rule pack-action-variable
      (head
        (pack-action
          (q-var ?variable) ?bindings (pa-slot ?slot)))
      (body (pack-lookup ?variable ?bindings ?slot)))
    (rule pack-action-application
      (head
        (pack-action
          (q-app (q-sym ?head) ?arguments)
          ?bindings
          (pa-apply ?head ?packed-arguments)))
      (body
        (pack-action-list
          ?arguments ?bindings ?packed-arguments)))

    (rule pack-action-list-empty
      (head
        (pack-action-list q-nil ?bindings pa-nil))
      (body))
    (rule pack-action-list-cons
      (head
        (pack-action-list
          (q-cons ?head ?tail)
          ?bindings
          (pa-cons ?packed-head ?packed-tail)))
      (body
        (pack-action ?head ?bindings ?packed-head)
        (pack-action-list ?tail ?bindings ?packed-tail)))

    (rule pack-lookup-head
      (head
        (pack-lookup
          ?variable
          (pack-bindings-cons
            (pack-binding ?variable ?slot) ?more)
          ?slot))
      (body))
    (rule pack-lookup-tail
      (head
        (pack-lookup
          ?variable
          (pack-bindings-cons ?binding ?more)
          ?slot))
      (body (pack-lookup ?variable ?more ?slot)))
  ))
