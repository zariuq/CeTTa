; Structural proof-calculus declarations over an explicitly named base
; language.  The presentation admits locally nameless patterns, ordered rule
; schemas, generic structural side conditions, and chronological articles.

(gslt-presentation-v1 CertificateGSLTArticleCoreV1
  (signature
    (operator proof-apply-v1 2)
    (operator proof-arguments-valid 3)
    (operator proof-article-admitted 7)
    (operator proof-bvar-v1 1)
    (operator proof-capability-declared 2)
    (operator proof-constructor 3)
    (operator proof-capability-kind 1)
    (operator proof-collection-kind 1)
    (operator proof-collection-v1 2)
    (operator proof-data-pattern 4)
    (operator proof-data-pattern-list 4)
    (operator proof-explicit-substitution-indexed-v1 4)
    (operator proof-explicit-substitution-v1 4)
    (operator proof-formal-index 4)
    (operator proof-formal-member 3)
    (operator proof-formal-v1 2)
    (operator proof-formals-valid 1)
    (operator proof-fvar-v1 1)
    (operator proof-judgment-pattern 3)
    (operator proof-judgment-pattern-list 3)
    (operator proof-lambda-v1 1)
    (operator proof-list-cons-v1 2)
    (operator proof-list-length 2)
    (operator proof-multi-lambda-v1 2)
    (operator proof-nat 1)
    (operator proof-nat-add 3)
    (operator proof-nat-less 2)
    (operator proof-node-list-valid 2)
    (operator proof-node-valid 2)
    (operator proof-node-v1 4)
    (operator proof-node-ref-v1 1)
    (operator proof-pattern-contains 4)
    (operator proof-pattern-list-contains 4)
    (operator proof-premise-ref-v1 1)
    (operator proof-reference-list-valid 1)
    (operator proof-reference-valid 1)
    (operator proof-root-policy 1)
    (operator proof-rule-admitted 6)
    (operator proof-rule-contains-formal 4)
    (operator proof-rule-formals-used 3)
    (operator proof-side-conditions-compile 4)
    (operator proof-subst-v1 2)
    (operator proof-succ-v1 1)
    (operator proof-unused-binder-indexed-v1 3)
    (operator proof-unused-binder-v1 3)
    (operator source-proof-article 7)
    (operator source-proof-base-constructor 3)
    (operator source-proof-calculus-constructor 3)
    (operator source-proof-capability 2)
    (operator source-proof-conversion 3)
    (operator source-proof-extension 2)
    (operator source-proof-judgment 3)
    (operator source-proof-rule 6)
  )
  (equations)
  (rewrites
    (rule proof-nat-zero
      (head (proof-nat proof-zero-v1))
      (body))
    (rule proof-nat-succ
      (head (proof-nat (proof-succ-v1 ?value)))
      (body (proof-nat ?value)))
    (rule proof-nat-add-zero
      (head (proof-nat-add ?left proof-zero-v1 ?left))
      (body (proof-nat ?left)))
    (rule proof-nat-add-succ
      (head
        (proof-nat-add
          ?left (proof-succ-v1 ?right) (proof-succ-v1 ?sum)))
      (body (proof-nat-add ?left ?right ?sum)))
    (rule proof-nat-less-zero
      (head (proof-nat-less proof-zero-v1 (proof-succ-v1 ?right)))
      (body (proof-nat ?right)))
    (rule proof-nat-less-succ
      (head
        (proof-nat-less
          (proof-succ-v1 ?left) (proof-succ-v1 ?right)))
      (body (proof-nat-less ?left ?right)))

    (rule proof-list-length-nil
      (head (proof-list-length proof-list-nil-v1 proof-zero-v1))
      (body))
    (rule proof-list-length-cons
      (head
        (proof-list-length
          (proof-list-cons-v1 ?item ?rest) (proof-succ-v1 ?length)))
      (body (proof-list-length ?rest ?length)))

    (rule proof-capability-binders
      (head (proof-capability-kind proof-cap-binders-v1))
      (body))
    (rule proof-capability-explicit-substitution
      (head
        (proof-capability-kind proof-cap-explicit-substitution-v1))
      (body))
    (rule proof-capability-collections
      (head (proof-capability-kind proof-cap-collections-v1))
      (body))
    (rule proof-capability-substitution-condition
      (head
        (proof-capability-kind proof-cap-substitution-condition-v1))
      (body))
    (rule proof-capability-unused-binder-condition
      (head
        (proof-capability-kind proof-cap-unused-binder-condition-v1))
      (body))
    (rule proof-capability-conversion
      (head
        (proof-capability-kind proof-cap-conversion-declaration-v1))
      (body))
    (rule proof-capability-is-declared
      (head (proof-capability-declared ?owner ?capability))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-capability ?owner ?capability)
        (proof-capability-kind ?capability)))

    (rule proof-constructor-from-base
      (head (proof-constructor ?owner ?name ?arity))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-base-constructor ?base ?name ?arity)))
    (rule proof-constructor-from-calculus
      (head (proof-constructor ?owner ?name ?arity))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-calculus-constructor ?owner ?name ?arity)))

    (rule proof-collection-vector
      (head (proof-collection-kind proof-vector-v1))
      (body))
    (rule proof-collection-hash-bag
      (head (proof-collection-kind proof-hash-bag-v1))
      (body))
    (rule proof-collection-hash-set
      (head (proof-collection-kind proof-hash-set-v1))
      (body))

    (rule proof-formal-member-head
      (head
        (proof-formal-member
          (proof-list-cons-v1
            (proof-formal-v1 ?name ?depth) ?rest)
          ?name ?depth))
      (body))
    (rule proof-formal-member-tail
      (head
        (proof-formal-member
          (proof-list-cons-v1 ?formal ?rest) ?name ?depth))
      (body (proof-formal-member ?rest ?name ?depth)))
    (rule proof-formal-index-head
      (head
        (proof-formal-index
          (proof-list-cons-v1
            (proof-formal-v1 ?name ?depth) ?rest)
          ?name ?depth proof-zero-v1))
      (body))
    (rule proof-formal-index-tail
      (head
        (proof-formal-index
          (proof-list-cons-v1 ?formal ?rest)
          ?name ?depth (proof-succ-v1 ?index)))
      (body (proof-formal-index ?rest ?name ?depth ?index)))
    (rule proof-formals-valid-nil
      (head (proof-formals-valid proof-list-nil-v1))
      (body))
    (rule proof-formals-valid-cons
      (head
        (proof-formals-valid
          (proof-list-cons-v1
            (proof-formal-v1 ?name ?depth) ?rest)))
      (body
        (proof-nat ?depth)
        (proof-formals-valid ?rest)))

    (rule proof-data-pattern-bvar
      (head
        (proof-data-pattern
          ?owner ?formals ?depth (proof-bvar-v1 ?index)))
      (body (proof-nat-less ?index ?depth)))
    (rule proof-data-pattern-fvar
      (head
        (proof-data-pattern
          ?owner ?formals ?depth (proof-fvar-v1 ?name)))
      (body (proof-formal-member ?formals ?name ?depth)))
    (rule proof-data-pattern-apply
      (head
        (proof-data-pattern
          ?owner ?formals ?depth (proof-apply-v1 ?name ?arguments)))
      (body
        (proof-constructor ?owner ?name ?arity)
        (proof-list-length ?arguments ?arity)
        (proof-data-pattern-list
          ?owner ?formals ?depth ?arguments)))
    (rule proof-data-pattern-lambda
      (head
        (proof-data-pattern
          ?owner ?formals ?depth (proof-lambda-v1 ?body)))
      (body
        (proof-capability-declared ?owner proof-cap-binders-v1)
        (proof-data-pattern
          ?owner ?formals (proof-succ-v1 ?depth) ?body)))
    (rule proof-data-pattern-multi-lambda
      (head
        (proof-data-pattern
          ?owner ?formals ?depth
          (proof-multi-lambda-v1 ?arity ?body)))
      (body
        (proof-capability-declared ?owner proof-cap-binders-v1)
        (proof-nat ?arity)
        (proof-nat-add ?depth ?arity ?inner-depth)
        (proof-data-pattern ?owner ?formals ?inner-depth ?body)))
    (rule proof-data-pattern-substitution
      (head
        (proof-data-pattern
          ?owner ?formals ?depth
          (proof-subst-v1 ?body ?replacement)))
      (body
        (proof-capability-declared
          ?owner proof-cap-explicit-substitution-v1)
        (proof-data-pattern
          ?owner ?formals (proof-succ-v1 ?depth) ?body)
        (proof-data-pattern
          ?owner ?formals ?depth ?replacement)))
    (rule proof-data-pattern-collection
      (head
        (proof-data-pattern
          ?owner ?formals ?depth
          (proof-collection-v1 ?kind ?elements)))
      (body
        (proof-capability-declared ?owner proof-cap-collections-v1)
        (proof-collection-kind ?kind)
        (proof-data-pattern-list
          ?owner ?formals ?depth ?elements)))
    (rule proof-data-pattern-list-nil
      (head
        (proof-data-pattern-list
          ?owner ?formals ?depth proof-list-nil-v1))
      (body))
    (rule proof-data-pattern-list-cons
      (head
        (proof-data-pattern-list
          ?owner ?formals ?depth
          (proof-list-cons-v1 ?pattern ?rest)))
      (body
        (proof-data-pattern ?owner ?formals ?depth ?pattern)
        (proof-data-pattern-list ?owner ?formals ?depth ?rest)))

    (rule proof-judgment-pattern-apply
      (head
        (proof-judgment-pattern
          ?owner ?formals (proof-apply-v1 ?head ?arguments)))
      (body
        (source-proof-judgment ?owner ?head ?arity)
        (proof-list-length ?arguments ?arity)
        (proof-data-pattern-list
          ?owner ?formals proof-zero-v1 ?arguments)))
    (rule proof-judgment-pattern-list-nil
      (head
        (proof-judgment-pattern-list
          ?owner ?formals proof-list-nil-v1))
      (body))
    (rule proof-judgment-pattern-list-cons
      (head
        (proof-judgment-pattern-list
          ?owner ?formals (proof-list-cons-v1 ?judgment ?rest)))
      (body
        (proof-judgment-pattern ?owner ?formals ?judgment)
        (proof-judgment-pattern-list ?owner ?formals ?rest)))

    (rule proof-pattern-contains-fvar
      (head
        (proof-pattern-contains
          ?name ?target-depth ?target-depth (proof-fvar-v1 ?name)))
      (body))
    (rule proof-pattern-contains-apply
      (head
        (proof-pattern-contains
          ?name ?target-depth ?ambient
          (proof-apply-v1 ?constructor ?arguments)))
      (body
        (proof-pattern-list-contains
          ?name ?target-depth ?ambient ?arguments)))
    (rule proof-pattern-contains-lambda
      (head
        (proof-pattern-contains
          ?name ?target-depth ?ambient (proof-lambda-v1 ?body)))
      (body
        (proof-pattern-contains
          ?name ?target-depth (proof-succ-v1 ?ambient) ?body)))
    (rule proof-pattern-contains-multi-lambda
      (head
        (proof-pattern-contains
          ?name ?target-depth ?ambient
          (proof-multi-lambda-v1 ?arity ?body)))
      (body
        (proof-nat-add ?ambient ?arity ?inner-depth)
        (proof-pattern-contains
          ?name ?target-depth ?inner-depth ?body)))
    (rule proof-pattern-contains-substitution-body
      (head
        (proof-pattern-contains
          ?name ?target-depth ?ambient
          (proof-subst-v1 ?body ?replacement)))
      (body
        (proof-pattern-contains
          ?name ?target-depth (proof-succ-v1 ?ambient) ?body)))
    (rule proof-pattern-contains-substitution-replacement
      (head
        (proof-pattern-contains
          ?name ?target-depth ?ambient
          (proof-subst-v1 ?body ?replacement)))
      (body
        (proof-pattern-contains
          ?name ?target-depth ?ambient ?replacement)))
    (rule proof-pattern-contains-collection
      (head
        (proof-pattern-contains
          ?name ?target-depth ?ambient
          (proof-collection-v1 ?kind ?elements)))
      (body
        (proof-pattern-list-contains
          ?name ?target-depth ?ambient ?elements)))
    (rule proof-pattern-list-contains-head
      (head
        (proof-pattern-list-contains
          ?name ?target-depth ?ambient
          (proof-list-cons-v1 ?pattern ?rest)))
      (body
        (proof-pattern-contains
          ?name ?target-depth ?ambient ?pattern)))
    (rule proof-pattern-list-contains-tail
      (head
        (proof-pattern-list-contains
          ?name ?target-depth ?ambient
          (proof-list-cons-v1 ?pattern ?rest)))
      (body
        (proof-pattern-list-contains
          ?name ?target-depth ?ambient ?rest)))
    (rule proof-rule-contains-formal-premise
      (head
        (proof-rule-contains-formal
          ?name ?depth ?premises ?conclusion))
      (body
        (proof-pattern-list-contains
          ?name ?depth proof-zero-v1 ?premises)))
    (rule proof-rule-contains-formal-conclusion
      (head
        (proof-rule-contains-formal
          ?name ?depth ?premises ?conclusion))
      (body
        (proof-pattern-contains
          ?name ?depth proof-zero-v1 ?conclusion)))
    (rule proof-rule-formals-used-nil
      (head
        (proof-rule-formals-used
          proof-list-nil-v1 ?premises ?conclusion))
      (body))
    (rule proof-rule-formals-used-cons
      (head
        (proof-rule-formals-used
          (proof-list-cons-v1
            (proof-formal-v1 ?name ?depth) ?rest)
          ?premises ?conclusion))
      (body
        (proof-rule-contains-formal
          ?name ?depth ?premises ?conclusion)
        (proof-rule-formals-used ?rest ?premises ?conclusion)))

    (rule proof-side-conditions-compile-nil
      (head
        (proof-side-conditions-compile
          ?owner ?formals proof-list-nil-v1 proof-list-nil-v1))
      (body))
    (rule proof-side-conditions-compile-substitution
      (head
        (proof-side-conditions-compile
          ?owner ?formals
          (proof-list-cons-v1
            (proof-explicit-substitution-v1
              ?ambient ?body-name ?replacement-name ?result-name)
            ?source-rest)
          (proof-list-cons-v1
            (proof-explicit-substitution-indexed-v1
              ?ambient ?body-index ?replacement-index ?result-index)
            ?compiled-rest)))
      (body
        (proof-capability-declared
          ?owner proof-cap-substitution-condition-v1)
        (proof-formal-index
          ?formals ?body-name (proof-succ-v1 ?ambient) ?body-index)
        (proof-formal-index
          ?formals ?replacement-name ?ambient ?replacement-index)
        (proof-formal-index
          ?formals ?result-name ?ambient ?result-index)
        (proof-side-conditions-compile
          ?owner ?formals ?source-rest ?compiled-rest)))
    (rule proof-side-conditions-compile-unused-binder
      (head
        (proof-side-conditions-compile
          ?owner ?formals
          (proof-list-cons-v1
            (proof-unused-binder-v1
              ?ambient ?body-name ?result-name)
            ?source-rest)
          (proof-list-cons-v1
            (proof-unused-binder-indexed-v1
              ?ambient ?body-index ?result-index)
            ?compiled-rest)))
      (body
        (proof-capability-declared
          ?owner proof-cap-unused-binder-condition-v1)
        (proof-formal-index
          ?formals ?body-name (proof-succ-v1 ?ambient) ?body-index)
        (proof-formal-index
          ?formals ?result-name ?ambient ?result-index)
        (proof-side-conditions-compile
          ?owner ?formals ?source-rest ?compiled-rest)))

    (rule proof-rule-admission
      (head
        (proof-rule-admitted
          ?owner ?id ?formals ?premises ?conclusion
          ?compiled-conditions))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-rule
          ?owner ?id ?formals ?premises ?conclusion ?conditions)
        (proof-formals-valid ?formals)
        (proof-judgment-pattern-list ?owner ?formals ?premises)
        (proof-judgment-pattern ?owner ?formals ?conclusion)
        (proof-rule-formals-used ?formals ?premises ?conclusion)
        (proof-side-conditions-compile
          ?owner ?formals ?conditions ?compiled-conditions)))

    (rule proof-arguments-valid-nil
      (head
        (proof-arguments-valid
          ?owner proof-list-nil-v1 proof-list-nil-v1))
      (body))
    (rule proof-arguments-valid-cons
      (head
        (proof-arguments-valid
          ?owner
          (proof-list-cons-v1
            (proof-formal-v1 ?name ?depth) ?formal-rest)
          (proof-list-cons-v1 ?argument ?argument-rest)))
      (body
        (proof-data-pattern
          ?owner proof-list-nil-v1 ?depth ?argument)
        (proof-arguments-valid
          ?owner ?formal-rest ?argument-rest)))
    (rule proof-reference-premise
      (head (proof-reference-valid (proof-premise-ref-v1 ?index)))
      (body (proof-nat ?index)))
    (rule proof-reference-node
      (head (proof-reference-valid (proof-node-ref-v1 ?index)))
      (body (proof-nat ?index)))
    (rule proof-reference-list-valid-nil
      (head (proof-reference-list-valid proof-list-nil-v1))
      (body))
    (rule proof-reference-list-valid-cons
      (head
        (proof-reference-list-valid
          (proof-list-cons-v1 ?reference ?rest)))
      (body
        (proof-reference-valid ?reference)
        (proof-reference-list-valid ?rest)))
    (rule proof-node-valid
      (head
        (proof-node-valid
          ?owner (proof-node-v1 ?id ?rule-id ?arguments ?references)))
      (body
        (proof-nat ?id)
        (source-proof-rule
          ?owner ?rule-id ?formals ?premises ?conclusion ?conditions)
        (proof-arguments-valid ?owner ?formals ?arguments)
        (proof-reference-list-valid ?references)
        (proof-list-length ?premises ?premise-length)
        (proof-list-length ?references ?premise-length)))
    (rule proof-node-list-valid-nil
      (head
        (proof-node-list-valid ?owner proof-list-nil-v1))
      (body))
    (rule proof-node-list-valid-cons
      (head
        (proof-node-list-valid
          ?owner (proof-list-cons-v1 ?node ?rest)))
      (body
        (proof-node-valid ?owner ?node)
        (proof-node-list-valid ?owner ?rest)))
    (rule proof-root-policy-compatible
      (head (proof-root-policy proof-compatible-roots-v1))
      (body))
    (rule proof-root-policy-require-rooted
      (head (proof-root-policy proof-require-rooted-v1))
      (body))
    (rule proof-article-admission
      (head
        (proof-article-admitted
          ?owner ?article-id ?context ?nodes ?root ?target ?root-policy))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-article
          ?owner ?article-id ?context ?nodes ?root ?target ?root-policy)
        (proof-judgment-pattern-list
          ?owner proof-list-nil-v1 ?context)
        (proof-node-list-valid ?owner ?nodes)
        (proof-nat ?root)
        (proof-judgment-pattern
          ?owner proof-list-nil-v1 ?target)
        (proof-root-policy ?root-policy)))
  ))
