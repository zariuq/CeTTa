; Finite refutation policy for the shared MatchDecision candidate selector.
; Unknown observations fall back to the ordered linear enumeration.  Every
; other row may refute only when the observed term shape proves a stored key
; incompatible.  The compiler expands these disjoint rules into the complete
; runtime table consumed by src/match_decision.c.

(gslt-presentation-v1 MatchDecisionPolicyV1
  (signature
    (operator candidate-policy 5))

  (equations)

  (rewrites
    (rule policy-unknown
      (head (candidate-policy unknown ?key ?arity ?identity fallback))
      (body))

    (rule policy-absent-wildcard
      (head (candidate-policy absent wildcard ?arity ?identity keep))
      (body))
    (rule policy-absent-literal
      (head (candidate-policy absent literal ?arity ?identity refute))
      (body))
    (rule policy-absent-expression-arity
      (head (candidate-policy absent expression-arity ?arity ?identity refute))
      (body))
    (rule policy-absent-expression-head
      (head (candidate-policy absent expression-head ?arity ?identity refute))
      (body))

    (rule policy-literal-wildcard
      (head (candidate-policy literal wildcard ?arity ?identity keep))
      (body))
    (rule policy-literal-literal-equal
      (head (candidate-policy literal literal ?arity equal keep))
      (body))
    (rule policy-literal-literal-different
      (head (candidate-policy literal literal ?arity different refute))
      (body))
    (rule policy-literal-expression-arity
      (head (candidate-policy literal expression-arity ?arity ?identity refute))
      (body))
    (rule policy-literal-expression-head
      (head (candidate-policy literal expression-head ?arity ?identity refute))
      (body))

    (rule policy-expression-wildcard
      (head (candidate-policy expression wildcard ?arity ?identity keep))
      (body))
    (rule policy-expression-literal
      (head (candidate-policy expression literal ?arity ?identity refute))
      (body))
    (rule policy-expression-arity-equal
      (head (candidate-policy expression expression-arity equal ?identity keep))
      (body))
    (rule policy-expression-arity-different
      (head (candidate-policy expression expression-arity different ?identity refute))
      (body))
    (rule policy-expression-head-arity-different
      (head (candidate-policy expression expression-head different ?identity refute))
      (body))
    (rule policy-expression-head-equal
      (head (candidate-policy expression expression-head equal equal keep))
      (body))
    (rule policy-expression-head-different
      (head (candidate-policy expression expression-head equal different refute))
      (body))
  ))
