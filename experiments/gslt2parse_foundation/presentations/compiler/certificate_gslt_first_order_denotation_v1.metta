; Denote the admitted first-order fragment of a compiled CertificateGSLT payload as
; a finite-Horn semantic GSLT.  Constructors and judgments become declared
; semantic operators.  Each proof rule becomes one backward goal-reduction
; schema from its conclusion to its ordered premises.
;
; This denotation deliberately has no clauses for binders, explicit
; substitution, collections, or compiled side conditions.  A consumer must
; compare denoted counts with the admitted payload and fail closed when a
; payload lies outside this exact fragment.

(gslt-presentation-v1 CertificateGSLTFirstOrderDenotationV1
  (signature
    (operator proof-apply-v1 2)
    (operator proof-artifact-v1 1)
    (operator proof-calculus-constructor-v1 3)
    (operator proof-formal-v1 2)
    (operator proof-fvar-v1 1)
    (operator proof-judgment-v1 3)
    (operator proof-list-cons-v1 2)
    (operator proof-rule-v1 6)
    (operator proof-semantic-denoted-v1 1)
    (operator proof-semantic-formal-index-v1 4)
    (operator proof-semantic-key-v1 3)
    (operator proof-semantic-nat-v1 2)
    (operator proof-semantic-pattern-list-v1 4)
    (operator proof-semantic-pattern-v1 4)
    (operator proof-semantic-required-v1 1)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-rule 3)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)
    (operator source-operator 3)
    (operator source-rule 2)
  )
  (equations)
  (rewrites
    (rule proof-semantic-nat-zero-v1
      (head (proof-semantic-nat-v1 proof-zero-v1 q-zero))
      (body))
    (rule proof-semantic-nat-succ-v1
      (head
        (proof-semantic-nat-v1
          (proof-succ-v1 ?value) (q-succ ?quoted)))
      (body (proof-semantic-nat-v1 ?value ?quoted)))

    (rule proof-semantic-formal-index-head-v1
      (head
        (proof-semantic-formal-index-v1
          (proof-list-cons-v1
            (proof-formal-v1 ?name ?depth) ?rest)
          ?name ?depth q-zero))
      (body))
    (rule proof-semantic-formal-index-tail-v1
      (head
        (proof-semantic-formal-index-v1
          (proof-list-cons-v1 ?formal ?rest)
          ?name ?depth (q-succ ?index)))
      (body
        (proof-semantic-formal-index-v1
          ?rest ?name ?depth ?index)))

    (rule proof-semantic-pattern-fvar-v1
      (head
        (proof-semantic-pattern-v1
          ?formals ?depth (proof-fvar-v1 ?name) (q-var ?index)))
      (body
        (proof-semantic-formal-index-v1
          ?formals ?name ?depth ?index)))
    (rule proof-semantic-pattern-apply-v1
      (head
        (proof-semantic-pattern-v1
          ?formals ?depth
          (proof-apply-v1 ?name ?arguments)
          (q-app (q-sym ?name) ?quoted-arguments)))
      (body
        (proof-semantic-pattern-list-v1
          ?formals ?depth ?arguments ?quoted-arguments)))
    (rule proof-semantic-pattern-list-nil-v1
      (head
        (proof-semantic-pattern-list-v1
          ?formals ?depth proof-list-nil-v1 q-nil))
      (body))
    (rule proof-semantic-pattern-list-cons-v1
      (head
        (proof-semantic-pattern-list-v1
          ?formals ?depth
          (proof-list-cons-v1 ?pattern ?rest)
          (q-cons ?quoted-pattern ?quoted-rest)))
      (body
        (proof-semantic-pattern-v1
          ?formals ?depth ?pattern ?quoted-pattern)
        (proof-semantic-pattern-list-v1
          ?formals ?depth ?rest ?quoted-rest)))

    (rule proof-semantic-calculus-constructor-v1
      (head
        (source-operator ?owner (q-sym ?name) ?quoted-arity))
      (body
        (proof-artifact-v1
          (proof-calculus-constructor-v1 ?owner ?name ?arity))
        (proof-semantic-nat-v1 ?arity ?quoted-arity)))
    (rule proof-semantic-calculus-constructor-required-v1
      (head
        (proof-semantic-required-v1
          (proof-semantic-key-v1
            proof-semantic-constructor-v1 ?owner ?name)))
      (body
        (proof-artifact-v1
          (proof-calculus-constructor-v1 ?owner ?name ?arity))))
    (rule proof-semantic-calculus-constructor-denoted-v1
      (head
        (proof-semantic-denoted-v1
          (proof-semantic-key-v1
            proof-semantic-constructor-v1 ?owner ?name)))
      (body
        (proof-artifact-v1
          (proof-calculus-constructor-v1 ?owner ?name ?arity))
        (proof-semantic-nat-v1 ?arity ?quoted-arity)))
    (rule proof-semantic-judgment-v1
      (head
        (source-operator ?owner (q-sym ?name) ?quoted-arity))
      (body
        (proof-artifact-v1
          (proof-judgment-v1 ?owner ?name ?arity))
        (proof-semantic-nat-v1 ?arity ?quoted-arity)))
    (rule proof-semantic-judgment-required-v1
      (head
        (proof-semantic-required-v1
          (proof-semantic-key-v1
            proof-semantic-judgment-v1 ?owner ?name)))
      (body
        (proof-artifact-v1
          (proof-judgment-v1 ?owner ?name ?arity))))
    (rule proof-semantic-judgment-denoted-v1
      (head
        (proof-semantic-denoted-v1
          (proof-semantic-key-v1
            proof-semantic-judgment-v1 ?owner ?name)))
      (body
        (proof-artifact-v1
          (proof-judgment-v1 ?owner ?name ?arity))
        (proof-semantic-nat-v1 ?arity ?quoted-arity)))
    (rule proof-semantic-rule-v1
      (head
        (source-rule
          ?owner
          (q-rule
            (q-sym ?id) ?quoted-conclusion ?quoted-premises)))
      (body
        (proof-artifact-v1
          (proof-rule-v1
            ?owner ?id ?formals ?premises ?conclusion
            proof-list-nil-v1))
        (proof-semantic-pattern-v1
          ?formals proof-zero-v1 ?conclusion ?quoted-conclusion)
        (proof-semantic-pattern-list-v1
          ?formals proof-zero-v1 ?premises ?quoted-premises)))
    (rule proof-semantic-rule-required-v1
      (head
        (proof-semantic-required-v1
          (proof-semantic-key-v1
            proof-semantic-rule-v1 ?owner ?id)))
      (body
        (proof-artifact-v1
          (proof-rule-v1
            ?owner ?id ?formals ?premises ?conclusion
            ?compiled-conditions))))
    (rule proof-semantic-rule-denoted-v1
      (head
        (proof-semantic-denoted-v1
          (proof-semantic-key-v1
            proof-semantic-rule-v1 ?owner ?id)))
      (body
        (proof-artifact-v1
          (proof-rule-v1
            ?owner ?id ?formals ?premises ?conclusion
            proof-list-nil-v1))
        (proof-semantic-pattern-v1
          ?formals proof-zero-v1 ?conclusion ?quoted-conclusion)
        (proof-semantic-pattern-list-v1
          ?formals proof-zero-v1 ?premises ?quoted-premises)))
  ))
