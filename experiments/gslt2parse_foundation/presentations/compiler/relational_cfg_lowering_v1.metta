(gslt-presentation-v1 RelationalCFGLoweringV1
  (signature
    (operator compile-cfg-chain 6)
    (operator compile-cfg-production 2)
    (operator compile-cfg-rule 2)
    (operator compile-cfg-state 1)
    (operator q-disjoint 2)
    (operator q-ground 1)
    (operator q-ground-list 1)
    (operator q-index-ne 2)
    (operator q-member 2)
    (operator q-not-member 2)
    (operator q-term-list-vars-in 2)
    (operator q-term-vars-in 2)
    (operator q-unique 1)
    (operator sir-call 2)
    (operator sir-items-cons 2)
    (operator sir-production 4)
    (operator sir-source-production 2)
  )
  (equations)
  (rewrites
    (rule compile-cfg-chain-empty
      (head
        (compile-cfg-chain
          ?cursor
          ?cursor
          q-nil
          sir-items-nil
          q-nil
          (q-cons ?cursor q-nil)))
      (body))
    (rule compile-cfg-chain-step
      (head
        (compile-cfg-chain
          ?current
          ?final
          (q-cons
            (q-app
              (q-sym parse)
              (q-cons
                ?grammar
                (q-cons
                  (q-var ?current)
                  (q-cons
                    (q-var ?value)
                    (q-cons (q-var ?next) q-nil)))))
            ?goals)
          (sir-items-cons
            (sir-call ?grammar (q-var ?value))
            ?items)
          (q-cons ?value ?outputs)
          (q-cons ?current ?cursors)))
      (body
        (q-ground ?grammar)
        (compile-cfg-chain
          ?next ?final ?goals ?items ?outputs ?cursors)))
    (rule compile-cfg-rule-linear
      (head
        (compile-cfg-rule
          (q-rule
            (q-sym ?rule)
            (q-app
              (q-sym parse)
              (q-cons
                (q-sym ?state)
                (q-cons
                  (q-var ?input)
                  (q-cons
                    ?value
                    (q-cons (q-var ?rest) q-nil)))))
            ?body)
          (sir-production ?rule ?state ?items ?value)))
      (body
        (compile-cfg-chain
          ?input ?rest ?body ?items ?outputs ?cursors)
        (q-unique ?outputs)
        (q-unique ?cursors)
        (q-disjoint ?outputs ?cursors)
        (q-term-vars-in ?value ?outputs)))
    (rule compile-cfg-production-from-source
      (head
        (compile-cfg-production
          ?quoted-rule
          (sir-source-production ?presentation ?production)))
      (body
        (source-rule ?presentation ?quoted-rule)
        (compile-cfg-rule ?quoted-rule ?production)))
    (rule compile-cfg-state-from-production
      (head (compile-cfg-state ?state))
      (body
        (compile-cfg-production
          ?quoted-rule
          (sir-source-production
            ?presentation
            (sir-production ?rule ?state ?items ?value)))))

    (rule q-disjoint-empty
      (head (q-disjoint q-nil ?right))
      (body))
    (rule q-disjoint-step
      (head (q-disjoint (q-cons ?head ?tail) ?right))
      (body
        (q-not-member ?head ?right)
        (q-disjoint ?tail ?right)))

    (rule q-ground-app
      (head (q-ground (q-app ?head ?arguments)))
      (body
        (q-ground ?head)
        (q-ground-list ?arguments)))
    (rule q-ground-int
      (head (q-ground (q-int ?value)))
      (body))
    (rule q-ground-str
      (head (q-ground (q-str ?value)))
      (body))
    (rule q-ground-sym
      (head (q-ground (q-sym ?name)))
      (body))
    (rule q-ground-list-empty
      (head (q-ground-list q-nil))
      (body))
    (rule q-ground-list-step
      (head (q-ground-list (q-cons ?head ?tail)))
      (body
        (q-ground ?head)
        (q-ground-list ?tail)))

    (rule q-index-ne-left-zero
      (head (q-index-ne q-zero (q-succ ?right)))
      (body))
    (rule q-index-ne-right-zero
      (head (q-index-ne (q-succ ?left) q-zero))
      (body))
    (rule q-index-ne-step
      (head (q-index-ne (q-succ ?left) (q-succ ?right)))
      (body (q-index-ne ?left ?right)))

    (rule q-member-head
      (head (q-member ?item (q-cons ?item ?tail)))
      (body))
    (rule q-member-tail
      (head (q-member ?item (q-cons ?head ?tail)))
      (body (q-member ?item ?tail)))
    (rule q-not-member-empty
      (head (q-not-member ?item q-nil))
      (body))
    (rule q-not-member-step
      (head (q-not-member ?item (q-cons ?head ?tail)))
      (body
        (q-index-ne ?item ?head)
        (q-not-member ?item ?tail)))

    (rule q-term-list-vars-in-empty
      (head (q-term-list-vars-in q-nil ?allowed))
      (body))
    (rule q-term-list-vars-in-step
      (head
        (q-term-list-vars-in (q-cons ?head ?tail) ?allowed))
      (body
        (q-term-vars-in ?head ?allowed)
        (q-term-list-vars-in ?tail ?allowed)))
    (rule q-term-vars-in-app
      (head (q-term-vars-in (q-app ?head ?arguments) ?allowed))
      (body
        (q-term-vars-in ?head ?allowed)
        (q-term-list-vars-in ?arguments ?allowed)))
    (rule q-term-vars-in-int
      (head (q-term-vars-in (q-int ?value) ?allowed))
      (body))
    (rule q-term-vars-in-str
      (head (q-term-vars-in (q-str ?value) ?allowed))
      (body))
    (rule q-term-vars-in-sym
      (head (q-term-vars-in (q-sym ?name) ?allowed))
      (body))
    (rule q-term-vars-in-var
      (head (q-term-vars-in (q-var ?index) ?allowed))
      (body (q-member ?index ?allowed)))

    (rule q-unique-empty
      (head (q-unique q-nil))
      (body))
    (rule q-unique-step
      (head (q-unique (q-cons ?head ?tail)))
      (body
        (q-not-member ?head ?tail)
        (q-unique ?tail)))
  ))
